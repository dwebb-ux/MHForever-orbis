#pragma once

#include "interfaces/IInputSystem.h"

#include <array>
#include <cstdint>

namespace mhf::platform::ps4 {

class Ps4Input final : public runtime::IInputSystem {
public:
    Ps4Input();
    ~Ps4Input() override;
    Ps4Input(const Ps4Input &) = delete;
    Ps4Input &operator=(const Ps4Input &) = delete;

    bool Poll() override;
    runtime::InputState State() const noexcept override;

private:
    std::int32_t handle_ = -1;
    runtime::InputState state_;
};

} // namespace mhf::platform::ps4
