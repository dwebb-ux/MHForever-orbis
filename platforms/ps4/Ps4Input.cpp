#include "Ps4Input.h"

#include <pad.h>

namespace mhf::platform::ps4 {

Ps4Input::Ps4Input() {
    handle_ = scePadOpen(SCE_USER_SERVICE_USER_ID_SYSTEM,
                         SCE_PAD_PORT_TYPE_STANDARD,
                         0,
                         nullptr);
}

Ps4Input::~Ps4Input() {
    if (handle_ >= 0) {
        scePadClose(handle_);
        handle_ = -1;
    }
}

bool Ps4Input::Poll() {
    if (handle_ < 0) return false;

    ScePadData data{};
    if (scePadRead(handle_, &data, 1) != 1) return false;

    state_.actions[static_cast<std::size_t>(runtime::InputAction::South)] =
        (data.buttons & SCE_PAD_BUTTON_CROSS) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::East)] =
        (data.buttons & SCE_PAD_BUTTON_CIRCLE) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::West)] =
        (data.buttons & SCE_PAD_BUTTON_SQUARE) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::North)] =
        (data.buttons & SCE_PAD_BUTTON_TRIANGLE) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::LeftShoulder)] =
        (data.buttons & SCE_PAD_BUTTON_L1) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::RightShoulder)] =
        (data.buttons & SCE_PAD_BUTTON_R1) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::LeftTrigger)] =
        data.leftTrigger > 128;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::RightTrigger)] =
        data.rightTrigger > 128;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::Start)] =
        (data.buttons & SCE_PAD_BUTTON_OPTIONS) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::Select)] =
        (data.buttons & SCE_PAD_BUTTON_TOUCH_PAD) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadUp)] =
        (data.buttons & SCE_PAD_BUTTON_UP) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadDown)] =
        (data.buttons & SCE_PAD_BUTTON_DOWN) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadLeft)] =
        (data.buttons & SCE_PAD_BUTTON_LEFT) != 0;
    state_.actions[static_cast<std::size_t>(runtime::InputAction::DPadRight)] =
        (data.buttons & SCE_PAD_BUTTON_RIGHT) != 0;

    state_.left_x = (static_cast<int>(data.leftStick.x) - 128) / 127.0f;
    state_.left_y = (128 - static_cast<int>(data.leftStick.y)) / 127.0f;
    state_.right_x = (static_cast<int>(data.rightStick.x) - 128) / 127.0f;
    state_.right_y = (128 - static_cast<int>(data.rightStick.y)) / 127.0f;
    return true;
}

runtime::InputState Ps4Input::State() const noexcept {
    return state_;
}

} // namespace mhf::platform::ps4
