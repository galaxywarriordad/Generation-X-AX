# PCSX2_AX Clean Staging Tree (Planning Phase Only — Stay Tuned!)

- **Status:** Planning phase only — implementation and appliance integration remain gated.
- **Boundary:** Policy-in-SDK (\x-sdk\ v1_36_2) / Thin Core (\libax_pcsx2_core\) / Thin Mode Adapters (\Ps2ConsoleAdapter\ for \--mode ps2\, \CohHArcadeAdapter\ for \--mode coh-h\ / PCSX2x6).
- **Target Budgets:** \Piccolo-AX\ (\<= 524288 KB\ / \512 MB\ \	oram\ \EROFS\ lz4hc,9, Linux 6.12-rt, \2:3:1\ isolation), Vulkan 1.3 (\5.00 GiB\ prune / \5.50 GiB\ VRAM ceiling), atomic \SHARE\ (\/userdata\) persistence via \.tmp -> fsync -> rename\, and \/dev/shm\ + \K01piccolo-sync\ cache handling.
