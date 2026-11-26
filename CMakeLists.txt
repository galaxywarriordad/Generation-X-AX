Run cmake --build build/pcsx2_ax -- -j 4
[1/61] Building CXX object CMakeFiles/ax_pcsx2_core.dir/core/GS.cpp.o
FAILED: [code=1] CMakeFiles/ax_pcsx2_core.dir/core/GS.cpp.o 
/usr/bin/clang++ -DPCSX2_AX_HAS_VULKAN_1_3=1 -DPCSX2_AX_PLANNING_ONLY=1 -DPCSX2_AX_SDK_VERSION_TARGET=\"v1_36_2\" -DPCSX2_AX_SHM_SYNC_DAEMON=\"K01piccolo-sync\" -DPCSX2_AX_VRAM_CEILING_GIB=5.50 -DPCSX2_AX_VRAM_PRUNE_GIB=5.00 -I/home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax -I/home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core -I/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc -O2 -g -DNDEBUG -std=c++20 -fvisibility=hidden -O2 -Wall -Wextra -Wpedantic -march=skylake -MD -MT CMakeFiles/ax_pcsx2_core.dir/core/GS.cpp.o -MF CMakeFiles/ax_pcsx2_core.dir/core/GS.cpp.o.d -o CMakeFiles/ax_pcsx2_core.dir/core/GS.cpp.o -c /home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core/GS.cpp
In file included from /home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core/GS.cpp:6:
/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc/Config.h:16:14: warning: explicitly defaulted equality comparison operator is implicitly deleted [-Wdefaulted-function-deleted]
   16 |         bool operator==(const CpuOptions&) const = default;
      |              ^
/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc/Config.h:13:111: note: defaulted 'operator==' is implicitly deleted because there is no viable 'operator==' for member 'Recompiler'
   13 |         struct RecompilerOptions { bool EnableFastmem = true; bool EnableVU0 = true; bool EnableVU1 = true; } Recompiler;
      |                                                                                                               ^
/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc/Config.h:16:52: note: replace 'default' with 'delete'
   16 |         bool operator==(const CpuOptions&) const = default;
      |                                                    ^~~~~~~
      |                                                    delete
/home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core/GS.cpp:7:10: fatal error: 'Gif_Unit.h' file not found
    7 | #include "Gif_Unit.h"
      |          ^~~~~~~~~~~~
1 warning and 1 error generated.
[2/61] Building CXX object CMakeFiles/ax_pcsx2_core.dir/core/CDVDcommon.cpp.o
FAILED: [code=1] CMakeFiles/ax_pcsx2_core.dir/core/CDVDcommon.cpp.o 
/usr/bin/clang++ -DPCSX2_AX_HAS_VULKAN_1_3=1 -DPCSX2_AX_PLANNING_ONLY=1 -DPCSX2_AX_SDK_VERSION_TARGET=\"v1_36_2\" -DPCSX2_AX_SHM_SYNC_DAEMON=\"K01piccolo-sync\" -DPCSX2_AX_VRAM_CEILING_GIB=5.50 -DPCSX2_AX_VRAM_PRUNE_GIB=5.00 -I/home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax -I/home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core -I/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc -O2 -g -DNDEBUG -std=c++20 -fvisibility=hidden -O2 -Wall -Wextra -Wpedantic -march=skylake -MD -MT CMakeFiles/ax_pcsx2_core.dir/core/CDVDcommon.cpp.o -MF CMakeFiles/ax_pcsx2_core.dir/core/CDVDcommon.cpp.o.d -o CMakeFiles/ax_pcsx2_core.dir/core/CDVDcommon.cpp.o -c /home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core/CDVDcommon.cpp
In file included from /home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core/CDVDcommon.cpp:7:
/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc/Config.h:16:14: warning: explicitly defaulted equality comparison operator is implicitly deleted [-Wdefaulted-function-deleted]
   16 |         bool operator==(const CpuOptions&) const = default;
      |              ^
/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc/Config.h:13:111: note: defaulted 'operator==' is implicitly deleted because there is no viable 'operator==' for member 'Recompiler'
   13 |         struct RecompilerOptions { bool EnableFastmem = true; bool EnableVU0 = true; bool EnableVU1 = true; } Recompiler;
      |                                                                                                               ^
/home/runner/work/Generation-X-AX/Generation-X-AX/build/pcsx2_ax/proto_inc/Config.h:16:52: note: replace 'default' with 'delete'
   16 |         bool operator==(const CpuOptions&) const = default;
      |                                                    ^~~~~~~
      |                                                    delete
/home/runner/work/Generation-X-AX/Generation-X-AX/engines/pcsx2_ax/core/CDVDcommon.cpp:8:10: fatal error: 'Host.h' file not found
    8 | #include "Host.h"
      |          ^~~~~~~~
1 warning and 1 error generated.
[3/61] Building CXX object CMakeFiles/ax_pcsx2_core.dir/core/CDVDisoReader.cpp.o
[4/61] Building CXX object CMakeFiles/ax_pcsx2_core.dir/core/CDVDdiscThread.cpp.o
ninja: build stopped: subcommand failed.
Error: Process completed with exit code 1.
