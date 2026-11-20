#pragma once

#include <array>
#include <cstdint>

namespace mhf::runtime {

enum class InputAction : std::uint8_t {
    South,
    East,
    West,
    North,
    LeftShoulder,
    RightShoulder,
    LeftTrigger,
    RightTrigger,
    Start,
    Select,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
};

struct InputState {
    std::array<bool, 14> actions{};
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
};

class IInputSystem {
public:
    virtual ~IInputSystem() = default;

    virtual bool Poll() = 0;
    virtual InputState State() const noexcept = 0;
};

} // namespace mhf::runtime
