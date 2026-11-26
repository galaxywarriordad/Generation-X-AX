#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ax::ui::retro {

enum class SnesButton : std::size_t {
    Up = 0,
    Down,
    Left,
    Right,
    Start,
    Select,
    A,
    B,
    X,
    Y,
    L,
    R,
    Count,
};

struct SnesControllerMap {
    std::array<std::uint32_t, static_cast<std::size_t>(SnesButton::Count)> codes{};
};

class SequentialInputBinder {
public:
    void Begin() noexcept {
        active_ = true;
        step_ = 0;
        map_ = {};
    }

    void Cancel() noexcept {
        active_ = false;
    }

    bool is_active() const noexcept { return active_; }

    std::size_t current_index() const noexcept { return step_; }

    SnesButton current_button() const noexcept {
        const std::size_t kCount = static_cast<std::size_t>(SnesButton::Count);
        return static_cast<SnesButton>(step_ < kCount ? step_ : (kCount - 1));
    }

    std::string current_prompt() const {
        if (!active_) return "BINDING COMPLETE";
        switch (current_button()) {
            case SnesButton::Up:     return "PRESS UP";
            case SnesButton::Down:   return "PRESS DOWN";
            case SnesButton::Left:   return "PRESS LEFT";
            case SnesButton::Right:  return "PRESS RIGHT";
            case SnesButton::Start:  return "PRESS START";
            case SnesButton::Select: return "PRESS SELECT";
            case SnesButton::A:      return "PRESS A";
            case SnesButton::B:      return "PRESS B";
            case SnesButton::X:      return "PRESS X";
            case SnesButton::Y:      return "PRESS Y";
            case SnesButton::L:      return "PRESS L";
            case SnesButton::R:      return "PRESS R";
            default:                 return "PRESS BUTTON";
        }
    }

    bool RecordInputCode(std::uint32_t input_code) noexcept {
        if (!active_) return false;
        const std::size_t kCount = static_cast<std::size_t>(SnesButton::Count);
        if (step_ < kCount) {
            map_.codes[step_] = input_code;
            ++step_;
        }
        if (step_ >= kCount) {
            active_ = false;
            return true;
        }
        return false;
    }

    const SnesControllerMap& result() const noexcept { return map_; }

private:
    bool active_{false};
    std::size_t step_{0};
    SnesControllerMap map_{};
};

} // namespace ax::ui::retro
