enum class EngineId : std::uint8_t {
  Rmg = 0,
  BlastEm = 1,
  Cannonball = 2,
  EDuke32 = 3,
};
```[cite: 10]

### 2. Update `shared/ax_retro_bridge.cpp`
Add the `EngineId::EDuke32` case inside `RetroBridge::engine_name()`:

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
  }
  return "unknown";
}
```[cite: 9]

*Verification:* After saving both files in `Generation-X-AX`, confirm `EngineId::EDuke32` compiles cleanly in `RetroBridge::engine_name()` with no missing-enum compiler warnings.

### 3. Add `AX_BUILD_EDUKE32` to the Root `CMakeLists.txt`
In the root `CMakeLists.txt` of `Generation-X-AX`, add the option and subdirectory guard alongside the other engines:

```cmake
option(AX_BUILD_EDUKE32 "Build EDuke32 FPS engine" ON)

if(AX_BUILD_EDUKE32)
  add_subdirectory(engines/eduke32)
endif()
