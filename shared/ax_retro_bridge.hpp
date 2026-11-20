enum class EngineId : std::uint8_t {
  Rmg = 0,
  BlastEm = 1,
  Cannonball = 2,
  EDuke32 = 3,
  Frontier = 4,
};
```[cite: 6]

2. **`shared/ax_retro_bridge.cpp`** — update `RetroBridge::engine_name()`:
```cpp
std::string_view RetroBridge::engine_name() const noexcept {
  switch (config_.engine) {
    case EngineId::Rmg:
      return "rmg";
    case EngineId::BlastEm:
      return "blastem";
    case EngineId::Cannonball:
      return "cannonball";
    case EngineId::EDuke32:
      return "eduke32";
    case EngineId::Frontier:
      return "frontier";
  }
  return "unknown";
}
```[cite: 5]

3. **Root `CMakeLists.txt`** — add the engine toggles and subdirectories alongside your existing engines:
```cmake
option(AX_BUILD_EDUKE32 "Build EDuke32 FPS engine" ON)
option(AX_BUILD_FRONTIER "Build Frontier: Elite II engine" ON)

if(AX_BUILD_EDUKE32)
  add_subdirectory(engines/eduke32)
endif()

if(AX_BUILD_FRONTIER)
  add_subdirectory(engines/frontier)
endif()
