# Mega Man Zero native port workspace (individual GBA titles)

This workspace is set up around per-game static recompilation projects (gbarecomp-style), using your locally provided ROMs/BIOS.

## ROMs (local, not versioned)
- MMZ1 (USA, Europe): Mega Man Zero (USA, Europe).gba (SHA1 193b14120119162518a73c70876f0b8bffdbd96e, 8 MiB)
- MMZ2 (USA): Mega Man Zero 2 (USA).gba (SHA1 c4d93a58f0f82c526dec8a3fdbda170336303689, 8 MiB)
- MMZ3 (USA): Mega Man Zero 3 (USA).gba (SHA1 403a78f2cad93d41e4b0f2e520ce08026531664b, 8 MiB)
- MMZ4 (USA): Mega Man Zero 4 (USA).gba (SHA1 596993205a1895a6f51e80749407fb069b907628, 16 MiB)
- GBA BIOS: gba_bios.bin (SHA1 300c20df6731a33952ded8c436f7f186d25d3492)

## Status
- MegaManZeroRecomp/: reference recomp project for MMZ1 (USA) (Phase 0).
- gbarecomp/: framework/tools.
- games/mmz2, games/mmz3, games/mmz4: scaffolding for MMZ2-MMZ4 (each will need its own validated game.toml, generated corpus, and validation).
- Unified launcher (select MMZ1-MMZ4): TBD (recommended: launcher that starts the selected per-game executable).

## Notes
- ROM-derived generated sources must not be redistributed without permission.
- Each game needs its own discovered/reviewed corpus (function roots, code copies, jump tables, resumes, save config, etc.).
