#include "mmz_extended_view.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

#include "gba_bus.h"
#include "gba_ppu.h"
#include "runtime_arm.h"
#include "runtime_bus_bridge.h"

extern "C" unsigned g_ws_extra_left;
extern "C" unsigned g_ws_extra_right;

namespace mmz {
namespace {

// MMZ4 layer streamers and publish points, verified by runtime tracing on
// B4ZE rev 0. The dispatcher at 0x08007638 passes the room descriptor in R1;
// the layer scroll helper at 0x0800531C runs immediately after each streamer.
constexpr std::uint32_t kFramePublish = 0x080026D0u;
constexpr std::uint32_t kScrollPublish = 0x080028ECu;
constexpr std::uint32_t kLayerPublish = 0x0800531Cu;
constexpr std::uint32_t kLayerDispatcher = 0x08007638u;
constexpr std::uint32_t kTextBgStreamer = 0x08003620u;
constexpr std::uint32_t kTextBgFullReload = 0x08003FE0u;
constexpr std::uint32_t kBg1FullReload = 0x08004F80u;
constexpr std::uint32_t kBg1Streamer = 0x08005124u;
constexpr std::uint32_t kBg1State = 0x02000AE4u;
constexpr std::uint32_t kBg2State = 0x02000B6Cu;
constexpr std::uint32_t kBg3State = 0x02000BF4u;
constexpr std::uint32_t kBg2Vram = 0x06002000u;
constexpr std::uint32_t kBg3Vram = 0x06003000u;
constexpr std::uint32_t kBg1Map = 0x0202CAD4u;
// BG0CNT shadow. MMZ4's world layers are BG1 at +2 and BG3 at +6; BG2 is
// disabled in the validated stage. BG3CNT's map base 6 selects 0x06003000.
constexpr std::uint32_t kBgcntShadow = 0x02000A04u;
constexpr std::uint32_t kScrollShadow = 0x02000A10u;
constexpr std::uint32_t kLayerBase = 0x02000AD0u;
constexpr std::uint32_t kLayerStride = 0x88u;
constexpr std::size_t kMapBytes = 0x1000u;
constexpr std::uint32_t kSyntheticReturn = 0xF0000001u;

struct StreamCall {
    bool valid = false;
    std::uint32_t state = 0;
    std::uint32_t target_pc = 0;
    std::uint32_t vram_base = 0;
    std::uint32_t r2 = 0;
    std::uint32_t r3 = 0;
    // MMZ4's streamers compare the transient X/Y in R1 against the persistent
    // state in R0. A synthetic call needs a shifted copy of the whole state so
    // tileset/palette/grid pointers survive while the column guard sees the
    // off-screen position.
    std::array<std::uint32_t, 32> previous{};
    std::array<std::uint32_t, 2> current{};
};

std::array<StreamCall, 3> g_observed{};
void (*g_previous_hook)(std::uint32_t) = nullptr;
bool g_in_synthetic_stream = false;
unsigned g_extra_left = 0;
unsigned g_extra_right = 0;
unsigned g_seen_layers = 0;
unsigned g_layers_updated_since_publish = 0;
std::array<std::array<std::array<std::uint8_t, kMapBytes>, 2>, 3>
    g_margin_maps{};
bool g_margin_valid[3][2]{};
std::array<std::array<std::uint32_t, 3>, 3> g_layout_signatures{};
std::array<bool, 3> g_layout_signature_valid{};
std::uint32_t g_bg1_data = 0;
std::int32_t g_bg1_camera_x = 0;
// The dispatcher supplies the room descriptor in R1; the layer state carries
// the room grid pointer at +0x14. Both are needed only for side-boundary
// validation, never to synthesize the authored maps themselves.
std::array<std::uint32_t, 3> g_descriptor{};
std::array<std::uint32_t, 3> g_room_grid{};
unsigned long long g_seen_state_epoch = 0;
std::array<std::array<std::int32_t, 2>, 3> g_last_layer_position{};
std::array<bool, 3> g_last_layer_position_valid{};
int g_last_pillarbox = -1;
int g_last_pillarbox_left = -1;
int g_last_pillarbox_right = -1;

bool trace_enabled();

bool trace_enabled() {
    static const bool enabled = std::getenv("GBARECOMP_MMZ_WS_TRACE") != nullptr;
    return enabled;
}

std::uint8_t* map_ptr(gba::GbaBus& bus, int layer_index) {
    if (layer_index == 0) {
        return bus.ewram_ptr() + (kBg1Map - 0x02000000u);
    }
    const std::uint32_t base = layer_index == 1 ? kBg2Vram : kBg3Vram;
    return bus.vram_ptr() + (base - 0x06000000u);
}

std::uint32_t read32(const gba::GbaBus& bus, std::uint32_t addr) {
    const std::uint8_t* p = nullptr;
    if (addr >= 0x02000000u && addr + 3u < 0x02040000u) {
        p = bus.ewram_ptr() + (addr - 0x02000000u);
    } else if (addr >= 0x03000000u && addr + 3u < 0x03008000u) {
        p = bus.iwram_ptr() + (addr - 0x03000000u);
    }
    if (!p) return 0;
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t read16(const gba::GbaBus& bus, std::uint32_t addr) {
    const std::uint8_t* p = nullptr;
    if (addr >= 0x02000000u && addr + 1u < 0x02040000u) {
        p = bus.ewram_ptr() + (addr - 0x02000000u);
    } else if (addr >= 0x03000000u && addr + 1u < 0x03008000u) {
        p = bus.iwram_ptr() + (addr - 0x03000000u);
    }
    if (!p) return 0;
    return static_cast<std::uint16_t>(p[0] |
        (static_cast<std::uint16_t>(p[1]) << 8));
}

bool peek8(const gba::GbaBus& bus, std::uint32_t addr, std::uint8_t& value) {
    if (addr >= 0x02000000u && addr < 0x02040000u) {
        value = bus.ewram_ptr()[addr - 0x02000000u];
        return true;
    }
    if (addr >= 0x08000000u) {
        const std::uint32_t off = (addr - 0x08000000u) & 0x01FFFFFFu;
        if (off < bus.rom_size()) {
            value = bus.rom_ptr()[off];
            return true;
        }
    }
    return false;
}

bool peek16(const gba::GbaBus& bus, std::uint32_t addr,
            std::uint16_t& value) {
    std::uint8_t lo = 0;
    std::uint8_t hi = 0;
    if (!peek8(bus, addr, lo) || !peek8(bus, addr + 1u, hi)) return false;
    value = static_cast<std::uint16_t>(lo |
        (static_cast<std::uint16_t>(hi) << 8));
    return true;
}

bool peek32(const gba::GbaBus& bus, std::uint32_t addr,
            std::uint32_t& value) {
    std::uint8_t bytes[4]{};
    for (unsigned i = 0; i < 4; ++i) {
        if (!peek8(bus, addr + i, bytes[i])) return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
        (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) |
        (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

struct AuthoredCell {
    std::uint32_t room = 0;
};

// MMZ4's room grid is a byte-per-screen table. Its header is
// [width, unused, default, height] followed by width * height room ids.
// The dispatcher supplies the current room descriptor in R1 and the layer
// state supplies the grid pointer at +0x14, so validation never has to guess
// a ROM family/variant table.
bool resolve_authored_cell(const gba::GbaBus& bus, int layer_index,
                           std::uint32_t fixed_x, std::uint32_t fixed_y,
                           AuthoredCell& out) {
    if (layer_index < 0 || layer_index > 2) return false;
    const std::uint32_t descriptor = g_descriptor[static_cast<std::size_t>(layer_index)];
    const std::uint32_t grid = g_room_grid[static_cast<std::size_t>(layer_index)];
    if (descriptor < 0x08000000u || grid < 0x08000000u) return false;

    std::uint32_t descriptor_room = 0;
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    if (!peek32(bus, descriptor, descriptor_room) ||
        !peek8(bus, grid, width) || !peek8(bus, grid + 3u, height)) {
        return false;
    }
    if (width == 0 || height == 0) return false;

    const std::uint32_t screen_x = fixed_x >> 8;
    const std::uint32_t screen_y = fixed_y >> 8;
    if (screen_x >= width || screen_y >= height) return false;

    std::uint8_t room = 0;
    if (!peek8(bus, grid + 4u + screen_y * width + screen_x, room)) {
        return false;
    }
    // The room descriptor stores a 1-based id, while the byte-per-screen grid
    // uses 0-based ids. MMZ3 happened to expose matching encodings; MMZ4 does
    // not, so subtract the bias before checking that the authored strip stays
    // inside the room currently being streamed.
    if (descriptor_room == 0u ||
        room != static_cast<std::uint8_t>(descriptor_room - 1u)) {
        if (trace_enabled()) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                std::fprintf(stderr,
                    "[mmz4:extended-view] room mismatch layer=%d desc=%08X "
                    "expected=%02X grid=%08X w=%u h=%u fixed=%u,%u screen=%u,%u "
                    "cell=%02X\n",
                    layer_index, descriptor, descriptor_room - 1u, grid, width,
                    height, fixed_x, fixed_y, screen_x, screen_y, room);
            }
        }
        return false;
    }
    out = {room};
    return true;
}

constexpr std::uint32_t kAuthoredScreenFixed = 1u << 8;

constexpr std::uint32_t authored_screens_crossed(std::uint32_t first,
                                                 std::uint32_t last) {
    return first <= last
        ? last / kAuthoredScreenFixed - first / kAuthoredScreenFixed + 1u
        : 0u;
}

static_assert(authored_screens_crossed(0u, kAuthoredScreenFixed - 1u) == 1u);
static_assert(authored_screens_crossed(kAuthoredScreenFixed - 1u,
                                       kAuthoredScreenFixed) == 2u);
static_assert(authored_screens_crossed(3u * kAuthoredScreenFixed + 7u,
                                       5u * kAuthoredScreenFixed + 9u) == 3u);

// Every screen column and row intersected by a widened strip must still be
// authored by the same room as the authentic edge. This prevents a room
// transition from being papered over with the neighbouring room's tiles.
bool authored_strip_matches_edge(const gba::GbaBus& bus, int layer_index,
                                 std::uint32_t authentic_x,
                                 std::uint32_t outer_x,
                                 std::uint32_t top_y,
                                 std::uint32_t bottom_y) {
    if (top_y > bottom_y) return false;
    const std::uint32_t first_x =
        authentic_x < outer_x ? authentic_x : outer_x;
    const std::uint32_t last_x =
        authentic_x < outer_x ? outer_x : authentic_x;
    const std::uint32_t first_x_screen = first_x / kAuthoredScreenFixed;
    const std::uint32_t last_x_screen = last_x / kAuthoredScreenFixed;
    const std::uint32_t first_y_screen = top_y / kAuthoredScreenFixed;
    const std::uint32_t last_y_screen = bottom_y / kAuthoredScreenFixed;

    for (std::uint32_t y_screen = first_y_screen;; ++y_screen) {
        const std::uint32_t y = y_screen == first_y_screen
            ? top_y : y_screen * kAuthoredScreenFixed;
        AuthoredCell authentic{};
        if (!resolve_authored_cell(bus, layer_index, authentic_x, y, authentic))
            return false;

        for (std::uint32_t x_screen = first_x_screen;; ++x_screen) {
            const std::uint32_t x = x_screen == first_x_screen
                ? first_x : x_screen * kAuthoredScreenFixed;
            AuthoredCell candidate{};
            if (!resolve_authored_cell(bus, layer_index, x, y, candidate) ||
                authentic.room != candidate.room) {
                return false;
            }
            if (x_screen == last_x_screen) break;
        }
        if (y_screen == last_y_screen) break;
    }
    return true;
}

void invalidate_layer(int layer_index) {
    g_margin_valid[layer_index][0] = false;
    g_margin_valid[layer_index][1] = false;
    g_seen_layers &= ~(1u << static_cast<unsigned>(layer_index));
}

void invalidate_all_layers() {
    for (int layer = 0; layer < 3; ++layer) invalidate_layer(layer);
}

void reset_presentation_caches() {
    invalidate_all_layers();
    g_seen_layers = 0;
    g_layers_updated_since_publish = 0;
    for (auto& call : g_observed) call.valid = false;
    g_layout_signature_valid.fill(false);
    g_last_layer_position_valid.fill(false);
}

void write32_iwram(gba::GbaBus& bus, std::uint32_t addr, std::uint32_t value) {
    if (addr < 0x03000000u || addr + 3u >= 0x03008000u) return;
    std::uint8_t* p = bus.iwram_ptr() + (addr - 0x03000000u);
    p[0] = static_cast<std::uint8_t>(value);
    p[1] = static_cast<std::uint8_t>(value >> 8);
    p[2] = static_cast<std::uint8_t>(value >> 16);
    p[3] = static_cast<std::uint8_t>(value >> 24);
}

class GuestWriteTransaction final : public gba::BusWriteObserver {
public:
    explicit GuestWriteTransaction(gba::GbaBus& bus) : bus_(bus) {}

    bool begin() {
        previous_ = bus_.exchange_write_observer(this);
        if (previous_) {
            bus_.set_write_observer(previous_);
            previous_ = nullptr;
            return false;
        }
        active_ = true;
        return true;
    }

    bool on_bus_write(gba::BusWriteRegion region, std::uint32_t off,
                      std::uint32_t, std::uint8_t width,
                      std::uint32_t old_value, std::uint32_t) override {
        if (region == gba::BusWriteRegion::Device) {
            device_accessed_ = true;
            return true;
        }
        writes_.push_back({region, off, width, old_value});
        return false;
    }

    void on_bus_read(std::uint32_t) override { device_accessed_ = true; }

    bool device_accessed() const { return device_accessed_; }

    void rollback_and_end() {
        if (!active_) return;
        bus_.set_write_observer(nullptr);
        for (auto it = writes_.rbegin(); it != writes_.rend(); ++it) {
            std::uint8_t* base = nullptr;
            std::size_t size = 0;
            switch (it->region) {
                case gba::BusWriteRegion::Ewram:
                    base = bus_.ewram_ptr(); size = 0x40000u; break;
                case gba::BusWriteRegion::Iwram:
                    base = bus_.iwram_ptr(); size = 0x8000u; break;
                case gba::BusWriteRegion::Pal:
                    base = bus_.pal_ptr(); size = 0x400u; break;
                case gba::BusWriteRegion::Vram:
                    base = bus_.vram_ptr(); size = 0x18000u; break;
                case gba::BusWriteRegion::Oam:
                    base = bus_.oam_ptr(); size = 0x400u; break;
                case gba::BusWriteRegion::Device:
                    break;
            }
            if (!base || it->off + it->width > size) continue;
            for (std::uint8_t byte = 0; byte < it->width; ++byte) {
                base[it->off + byte] = static_cast<std::uint8_t>(
                    it->old_value >> (byte * 8u));
            }
        }
        bus_.set_write_observer(previous_);
        active_ = false;
    }

    ~GuestWriteTransaction() override { rollback_and_end(); }

private:
    struct Write {
        gba::BusWriteRegion region;
        std::uint32_t off;
        std::uint8_t width;
        std::uint32_t old_value;
    };
    gba::GbaBus& bus_;
    gba::BusWriteObserver* previous_ = nullptr;
    std::vector<Write> writes_;
    bool active_ = false;
    bool device_accessed_ = false;
};

bool run_guest_streamer(gba::GbaBus& bus,
                        const StreamCall& call,
                        int horizontal_offset,
                        int layer_index,
                        int side_index) {
    const std::uint32_t original_sp = g_cpu.R[13];
    if (original_sp < 0x03000200u || original_sp > 0x03008000u) return false;

    constexpr std::uint32_t kReserve = 0x200u;
    const std::uint32_t scratch = original_sp - kReserve;
    if (scratch < 0x03000000u) return false;
    std::uint8_t* scratch_ptr = bus.iwram_ptr() + (scratch - 0x03000000u);
    std::array<std::uint8_t, kReserve> saved_stack{};
    std::memcpy(saved_stack.data(), scratch_ptr, saved_stack.size());
    constexpr std::array<std::uint8_t, 16> kStackCanary = {
        0x4Du, 0x4Du, 0x5Au, 0x2Du, 0x57u, 0x53u, 0x2Du, 0x43u,
        0x41u, 0x4Eu, 0x41u, 0x52u, 0x59u, 0x21u, 0xA5u, 0x5Au};
    std::memcpy(scratch_ptr, kStackCanary.data(), kStackCanary.size());
    std::uint8_t* live_map = map_ptr(bus, layer_index);
    std::array<std::uint8_t, kMapBytes> saved_map{};
    std::memcpy(saved_map.data(), live_map, saved_map.size());
    const bool cache_was_valid = g_margin_valid[layer_index][side_index];
    if (cache_was_valid) {
        std::memcpy(live_map, g_margin_maps[layer_index][side_index].data(),
                    kMapBytes);
    }

    const ArmCpuState saved_cpu = g_cpu;
    const std::uint32_t saved_call_depth = runtime_call_stack_depth();
    if (saved_call_depth > 1024u) {
        std::memcpy(live_map, saved_map.data(), saved_map.size());
        std::memcpy(scratch_ptr, saved_stack.data(), saved_stack.size());
        return false;
    }
    std::array<std::uint32_t, 1024> saved_call_stack{};
    if (saved_call_depth != 0) {
        std::memcpy(saved_call_stack.data(), runtime_call_stack_data(),
                    saved_call_depth * sizeof(std::uint32_t));
    }
    const unsigned saved_shadow = g_runtime_shadow_tick;
    const unsigned long long saved_shadow_cycles = g_runtime_shadow_cycles;
    const unsigned long long saved_idle_epoch = g_idle_disturb_epoch;
    const unsigned saved_insn_trace = g_runtime_insn_trace;
    // Keep the synthetic state above the downward-growing guest stack. A full
    // 32-word copy preserves every tileset/chunk field MMZ4 may read.
    const std::uint32_t previous_ptr = scratch + 0x160u;
    const std::uint32_t current_ptr = scratch + 0x1E0u;
    auto previous = call.previous;
    auto current = call.current;
    previous[0] += static_cast<std::uint32_t>(horizontal_offset);
    current[0] += static_cast<std::uint32_t>(horizontal_offset);
    for (std::size_t i = 0; i < previous.size(); ++i) {
        write32_iwram(bus, previous_ptr + static_cast<std::uint32_t>(i * 4u),
                      previous[i]);
    }
    for (std::size_t i = 0; i < current.size(); ++i) {
        write32_iwram(bus, current_ptr + static_cast<std::uint32_t>(i * 4u),
                      current[i]);
    }

    GuestWriteTransaction transaction(bus);
    if (!transaction.begin()) {
        std::memcpy(live_map, saved_map.data(), saved_map.size());
        std::memcpy(scratch_ptr, saved_stack.data(), saved_stack.size());
        return false;
    }

    g_runtime_shadow_tick = 1u;
    g_runtime_insn_trace = 0u;
    const bool full_seed = !cache_was_valid;
    const std::uint32_t target_pc = full_seed
        ? (layer_index == 0 ? kBg1FullReload : kTextBgFullReload)
        : call.target_pc;
    // Shift both the state copy and the transient position so the streamer's
    // 16-pixel column guard sees the off-screen coordinate.
    g_cpu.R[0] = previous_ptr;
    g_cpu.R[1] = current_ptr;
    g_cpu.R[2] = call.r2;
    g_cpu.R[3] = call.r3;
    g_cpu.R[13] = scratch + 0x140u;
    g_cpu.R[14] = kSyntheticReturn;
    g_cpu.cpsr |= CPSR_T_BIT;
    runtime_call_push_return(kSyntheticReturn);
    runtime_dispatch(target_pc | 1u);
    runtime_call_cancel_return(kSyntheticReturn);
    runtime_call_stack_restore(saved_call_stack.data(), saved_call_depth);
    const bool safe = !transaction.device_accessed() &&
        std::memcmp(scratch_ptr, kStackCanary.data(), kStackCanary.size()) == 0;
    if (!safe && trace_enabled()) {
        std::fprintf(stderr,
            "[mmz:extended-view] rejected synthetic layer=%d side=%d "
            "device=%d stack_canary=%d target=%08X\n",
            layer_index + 1, side_index, transaction.device_accessed(),
            std::memcmp(scratch_ptr, kStackCanary.data(),
                        kStackCanary.size()) == 0,
            target_pc);
    }
    if (safe) {
        std::memcpy(g_margin_maps[static_cast<std::size_t>(layer_index)]
                                 [static_cast<std::size_t>(side_index)].data(),
                    live_map, kMapBytes);
        g_margin_valid[layer_index][side_index] = true;
    }
    transaction.rollback_and_end();
    std::memcpy(live_map, saved_map.data(), saved_map.size());

    g_runtime_shadow_tick = saved_shadow;
    g_runtime_shadow_cycles = saved_shadow_cycles;
    g_idle_disturb_epoch = saved_idle_epoch;
    g_runtime_insn_trace = saved_insn_trace;
    g_cpu = saved_cpu;
    std::memcpy(scratch_ptr, saved_stack.data(), saved_stack.size());
    return safe;
}

void publish_authored_layout(gba::GbaBus& bus) {
    const std::uint8_t* io = bus.io().raw();
    auto io16 = [io](std::uint32_t off) {
        return static_cast<std::uint16_t>(
            io[off] | (static_cast<std::uint16_t>(io[off + 1]) << 8));
    };
    const std::uint16_t dispcnt = io16(0x00u);
    // MMZ4 streams BG1 into the EWRAM layer map and the second world layer
    // into VRAM base 6, which BG3CNT selects (0x4647 at the validated stage).
    // BG2CNT is disabled there, so validating a hypothetical BG2 would reject
    // all authored margins.
    constexpr std::uint32_t kLayerBg[2] = {1u, 3u};
    constexpr std::uint32_t kExpectedBases[2] = {2u, 6u};
    constexpr std::uint32_t kExpectedSizes[2] = {1u, 1u};
    bool layout_supported = (dispcnt & 7u) == 0u;
    for (int layer = 0; layer < 2; ++layer) {
        const std::uint16_t bgcnt =
            read16(bus, kBgcntShadow + kLayerBg[layer] * 2u);
        const std::uint32_t size = (bgcnt >> 14) & 3u;
        const std::uint32_t map_base = (bgcnt >> 8) & 31u;
        layout_supported = layout_supported &&
            size == kExpectedSizes[layer] &&
            map_base == kExpectedBases[layer];
    }
    if (!layout_supported) invalidate_all_layers();

    // Only the two world layers are streamed through the hooked dispatcher.
    // The unused layer's margins stay transparent rather than showing the
    // rejected wrapped seam.
    const bool authored_ready = layout_supported && (g_seen_layers & 0x3u) != 0;
    gba::g_ws_authored_margin_layers = authored_ready ? 1 : 0;
    gba::g_ws_pillarbox = authored_ready ? 0 : 1;
    gba::g_ws_pillarbox_left = 0;
    gba::g_ws_pillarbox_right = 0;

    if (authored_ready && g_last_layer_position_valid[0]) {
        const std::int64_t raw_x = g_last_layer_position[0][0];
        const std::int64_t raw_y = g_last_layer_position[0][1];
        if (raw_x >= 0 && raw_y >= 0) {
            const std::uint32_t world_left = static_cast<std::uint32_t>(raw_x);
            const std::uint32_t world_top = static_cast<std::uint32_t>(raw_y);
            const std::uint32_t world_bottom = world_top + 159u;
            if (g_extra_left != 0u) {
                const bool safe = world_left >= g_extra_left &&
                    authored_strip_matches_edge(bus, 0, world_left,
                        world_left - g_extra_left, world_top, world_bottom);
                gba::g_ws_pillarbox_left = safe ? 0 : 1;
            }
            if (g_extra_right != 0u) {
                const std::uint32_t authentic_right = world_left + 239u;
                const bool safe = authored_strip_matches_edge(bus, 0,
                    authentic_right, authentic_right + g_extra_right,
                    world_top, world_bottom);
                gba::g_ws_pillarbox_right = safe ? 0 : 1;
            }
        }
    }

    if (trace_enabled() &&
        (g_last_pillarbox != gba::g_ws_pillarbox ||
         g_last_pillarbox_left != gba::g_ws_pillarbox_left ||
         g_last_pillarbox_right != gba::g_ws_pillarbox_right)) {
        std::fprintf(stderr,
            "[mmz4:extended-view] margins global/left/right=%d/%d/%d "
            "layer_x=%d grid=%08X dispcnt=%04X seen=%u\n",
            gba::g_ws_pillarbox, gba::g_ws_pillarbox_left,
            gba::g_ws_pillarbox_right, g_bg1_camera_x, g_bg1_data,
            dispcnt, g_seen_layers);
        g_last_pillarbox = gba::g_ws_pillarbox;
        g_last_pillarbox_left = gba::g_ws_pillarbox_left;
        g_last_pillarbox_right = gba::g_ws_pillarbox_right;
    }
    g_layers_updated_since_publish = 0;
}

void synthesize_layer(gba::GbaBus& bus, int layer_index) {
    StreamCall& call = g_observed[static_cast<std::size_t>(layer_index)];
    if (!call.valid) return;
    const std::uint32_t expected_state = layer_index == 0 ? kBg1State :
        layer_index == 1 ? kBg2State : kBg3State;
    if (call.state != expected_state) {
        call.valid = false;
        return;
    }

    const std::array<std::uint32_t, 3> signature = {
        call.previous[3], call.previous[4], call.previous[5]};
    if (!g_layout_signature_valid[layer_index] ||
        g_layout_signatures[layer_index] != signature) {
        invalidate_layer(layer_index);
        g_layout_signatures[layer_index] = signature;
        g_layout_signature_valid[layer_index] = true;
        if (trace_enabled()) {
            std::fprintf(stderr,
                "[mmz4:extended-view] layer=%d tiles=%08X pal=%08X grid=%08X "
                "camera=%d,%d\n",
                layer_index + 1, signature[0], signature[1], signature[2],
                static_cast<std::int32_t>(call.current[0]),
                static_cast<std::int32_t>(call.current[1]));
        }
    }

    const std::array<std::int32_t, 2> position = {
        static_cast<std::int32_t>(call.current[0]),
        static_cast<std::int32_t>(call.current[1])};
    if (g_last_layer_position_valid[layer_index]) {
        const auto& last = g_last_layer_position[layer_index];
        const std::int64_t dx = static_cast<std::int64_t>(position[0]) - last[0];
        const std::int64_t dy = static_cast<std::int64_t>(position[1]) - last[1];
        if (dx < -64 || dx > 64 || dy < -64 || dy > 64)
            invalidate_layer(layer_index);
    }
    g_last_layer_position[layer_index] = position;
    g_last_layer_position_valid[layer_index] = true;

    g_in_synthetic_stream = true;
    const bool left_ok = run_guest_streamer(bus, call,
        -static_cast<int>(g_extra_left), layer_index, 0);
    const bool right_ok = run_guest_streamer(bus, call,
        static_cast<int>(g_extra_right), layer_index, 1);
    g_in_synthetic_stream = false;
    if (left_ok && right_ok) {
        g_seen_layers |= 1u << static_cast<unsigned>(layer_index);
        g_layers_updated_since_publish |=
            1u << static_cast<unsigned>(layer_index);
        if (layer_index == 0) {
            g_bg1_data = call.previous[5];
            g_bg1_camera_x = position[0];
        }
    } else {
        invalidate_layer(layer_index);
    }
    call.valid = false;
}

void extended_view_hook(std::uint32_t pc) {
    if (g_in_synthetic_stream) {
        return;
    }
    if (g_previous_hook) g_previous_hook(pc);

    // Fixed-width sessions never change these values after installation.
    // Resize-driven sessions update the runtime globals whenever the drawable
    // aspect changes. Adopt that geometry before running any more guest-side
    // presentation work, and discard maps synthesized for the prior offsets.
    if (g_extra_left != g_ws_extra_left ||
        g_extra_right != g_ws_extra_right) {
        g_extra_left = g_ws_extra_left;
        g_extra_right = g_ws_extra_right;
        reset_presentation_caches();
        g_last_pillarbox = g_last_pillarbox_left =
            g_last_pillarbox_right = -1;
        if (trace_enabled()) {
            std::fprintf(stderr,
                "[mmz4:extended-view] live geometry changed: margins=%u/%u "
                "logical=%ux160\n",
                g_extra_left, g_extra_right,
                240u + g_extra_left + g_extra_right);
        }
    }

    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) return;
    if (g_seen_state_epoch != g_runtime_state_epoch) {
        g_seen_state_epoch = g_runtime_state_epoch;
        reset_presentation_caches();
    }

    if (pc == kLayerDispatcher) {
        const std::uint32_t base = g_cpu.R[0];
        if (base >= kLayerBase &&
            (base - kLayerBase) % kLayerStride == 0u) {
            const std::uint32_t index =
                (base - kLayerBase) / kLayerStride;
            if (index < 3u) {
                g_descriptor[index] = g_cpu.R[1];
            }
        }
        return;
    }

    if (pc == kBg1Streamer || pc == kBg1FullReload ||
        pc == kTextBgStreamer || pc == kTextBgFullReload) {
        const std::uint32_t vram_base = g_cpu.R[2];
        int layer_index = -1;
        if ((pc == kBg1Streamer || pc == kBg1FullReload) &&
            g_cpu.R[0] == kBg1State && vram_base == kBg1Map) {
            layer_index = 0;
        } else if (pc == kTextBgStreamer || pc == kTextBgFullReload) {
            layer_index = vram_base == kBg2Vram ? 1 :
                          vram_base == kBg3Vram ? 2 : -1;
        }
        if (layer_index < 0 || layer_index > 1) return;
        if (layer_index == 1 && g_cpu.R[0] != kBg2State) return;
        if (pc == kBg1FullReload || pc == kTextBgFullReload)
            invalidate_layer(layer_index);
        StreamCall& call = g_observed[static_cast<std::size_t>(layer_index)];
        call.valid = true;
        call.state = g_cpu.R[0];
        call.target_pc = pc;
        call.vram_base = vram_base;
        call.r2 = g_cpu.R[2];
        call.r3 = g_cpu.R[3];
        for (std::size_t i = 0; i < call.previous.size(); ++i) {
            call.previous[i] = read32(*bus, g_cpu.R[0] +
                static_cast<std::uint32_t>(i * 4u));
        }
        for (std::size_t i = 0; i < call.current.size(); ++i) {
            call.current[i] = read32(*bus, g_cpu.R[1] +
                static_cast<std::uint32_t>(i * 4u));
        }
        g_room_grid[static_cast<std::size_t>(layer_index)] =
            call.previous[5];
        return;
    }

    if (pc == kLayerPublish) {
        const std::uint32_t shadow = g_cpu.R[1];
        if (shadow >= kScrollShadow &&
            (shadow - kScrollShadow) % 4u == 0u) {
            const std::uint32_t layer_index = (shadow - kScrollShadow) / 4u;
            if (layer_index < 2u) {
                synthesize_layer(*bus, static_cast<int>(layer_index));
            }
        }
        publish_authored_layout(*bus);
        return;
    }

    if (pc == kFramePublish || pc == kScrollPublish) {
        publish_authored_layout(*bus);
    }
}

int margin_tilemap_provider(int bg, int hw_x, int screen_y,
                            std::uint16_t* out_entry) {
    if (!out_entry) return 0;
    const int layer_index = bg == 1 ? 0 : bg == 3 ? 1 : -1;
    if (layer_index < 0) return 0;
    const int side_index = hw_x < 0 ? 0 : 1;
    if (!g_margin_valid[layer_index][side_index]) return 0;
    gba::GbaBus* bus = gbarecomp::active_bus();
    if (!bus) return 0;
    const std::uint8_t* io = bus->io().raw();
    auto io16 = [io](std::uint32_t off) {
        return static_cast<std::uint16_t>(
            io[off] | (static_cast<std::uint16_t>(io[off + 1]) << 8));
    };
    const std::uint16_t bgcnt = io16(0x08u + static_cast<std::uint32_t>(bg * 2));
    const std::uint32_t size_code = (bgcnt >> 14) & 3u;
    // The captured MMZ maps at the validated gameplay checkpoint are the
    // horizontal 512x256 shape. Pillarbox rather than guess at other layouts.
    const std::uint32_t expected_base = static_cast<std::uint32_t>(
        bg == 1 ? 2u : 6u);
    if (size_code != 1u || ((bgcnt >> 8) & 31u) != expected_base) return 0;
    const std::uint32_t scroll_off = 0x10u + static_cast<std::uint32_t>(bg * 4);
    const std::uint32_t hofs = io16(scroll_off) & 0x01FFu;
    const std::uint32_t vofs = io16(scroll_off + 2u) & 0x01FFu;
    const std::uint32_t tex_x =
        static_cast<std::uint32_t>(hw_x + static_cast<int>(hofs)) & 511u;
    const std::uint32_t tex_y =
        (static_cast<std::uint32_t>(screen_y) + vofs) & 255u;
    const std::uint32_t tile_x = tex_x >> 3;
    const std::uint32_t tile_y = tex_y >> 3;
    const std::uint32_t block = tile_x >> 5;
    const std::uint32_t map_off = block * 0x800u +
        ((tile_y & 31u) * 32u + (tile_x & 31u)) * 2u;
    const auto& map = g_margin_maps[static_cast<std::size_t>(layer_index)]
                                   [static_cast<std::size_t>(side_index)];
    *out_entry = static_cast<std::uint16_t>(
        map[map_off] | (static_cast<std::uint16_t>(map[map_off + 1]) << 8));
    return gba::kWsTilemapReplace;
}

}  // namespace

void install_extended_view(unsigned extra_left, unsigned extra_right) {
    // A reinstall may change the hidden previous-provider delegate while the
    // public pointer is unchanged. Revoke the old promise before rebuilding it.
    gba::clear_read_only_wide_bg_providers();
    g_extra_left = extra_left;
    g_extra_right = extra_right;
    g_seen_state_epoch = g_runtime_state_epoch;
    reset_presentation_caches();
    g_last_pillarbox = g_last_pillarbox_left = g_last_pillarbox_right = -1;
    // MMZ4 has no verified HUD/OBJ/spawn remaps yet. Keep their native
    // placement and contribute only the authored BG1/BG2 margin maps.
    gba::g_ws_authored_margin_layers = 0;
    gba::g_ws_pillarbox = 1;
    gba::g_ws_pillarbox_left = 0;
    gba::g_ws_pillarbox_right = 0;
    gba::g_ws_tilemap_provider = margin_tilemap_provider;
    if (g_runtime_fn_entry_hook == extended_view_hook) return;
    g_previous_hook = g_runtime_fn_entry_hook;
    g_runtime_fn_entry_hook = extended_view_hook;
}

}  // namespace mmz
