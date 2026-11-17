# Writes ${OUT} with the engine's current git revision, only when it changed,
# so diagnostic replays always name the engine that was actually compiled.
# -DOVERRIDE=<rev> wins (release builds from a copied tree without .git).
if(OVERRIDE)
    set(rev "${OVERRIDE}")
else()
    set(rev "local")
endif()
set(text "#pragma once\n#define GBARECOMP_ENGINE_REVISION \"${rev}\"\n")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
    if(old STREQUAL text)
        return()
    endif()
endif()
file(WRITE "${OUT}" "${text}")
