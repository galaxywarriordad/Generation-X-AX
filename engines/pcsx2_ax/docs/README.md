# ⚙️ `PCSX2_AX` Staging & Core Architecture (`libax_pcsx2_core`)

- **Status:** **Prototype Phase** — architecture and core boundaries are staged for verification (`stay tuned!`).
- **Boundary:** Policy-in-SDK (`ax-sdk` `v1_36_2`) / Thin Core (`libax_pcsx2_core`) / Thin Mode Adapters (`Ps2ConsoleAdapter` for `--mode ps2`, `CohHArcadeAdapter` for `--mode coh-h` / `PCSX2x6`).
- **Staging Footprint:** `48` core files, `26` PS2 adapter files, `19` `COH-H` adapter files; `586` unchanged upstream files avoided, `230` modified files, and `43` unique `COH-H` files tracked.
- **Target Budgets & Guardrails:** `Piccolo-AX` (`<= 524288 KB` / `512 MB` `toram` `EROFS` `lz4hc,9`, Linux `6.12-rt`, `2:3:1` isolation), Vulkan 1.3 (`5.00 GiB` prune / `5.50 GiB` VRAM ceiling on `GTX 980 Ti` `580.*`), atomic `/userdata/saves/ps2/` and `/userdata/saves/pcsx2x6/` persistence via `.tmp -> fsync -> rename`, and `/dev/shm` + `K01piccolo-sync` volatile cache handling.
- **CHD Packing (`chdman`):** Use **MAME 0.262+** for `createdvd -hs 2048` or `16384 -c zstd` (and `createcd -c zstd` for CD images); use `-c zlib` on `MAME 0.255–0.261` and `createcd` on `MAME <= 0.254`.
