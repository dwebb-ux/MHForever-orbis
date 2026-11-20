#include "PspNetworkSystem.h"

#include <utility>

namespace mhf::psp::adapters {

PspNetworkSystem::PspNetworkSystem()
    : PspNetworkSystem("mhf-psp-local") {}

PspNetworkSystem::PspNetworkSystem(std::string room_code)
    : room_code_(std::move(room_code)) {}

PspNetworkSystem::~PspNetworkSystem() = default;

bool PspNetworkSystem::Initialize() {
    if (room_) return true;

    net::RoomDescription description;
    description.game_id = "mhf-psp";
    description.region = "psp";
    description.build = "runtime";
    description.mode = net::Mode::Packets;

    net::JoinError error = net::JoinError::None;
    room_ = network_.create(room_code_, description, "PSP", &error);
    return room_ != nullptr && error == net::JoinError::None;
}

void PspNetworkSystem::Shutdown() noexcept {
    if (room_) {
        room_->leave();
        room_.reset();
    }
}

runtime::NetworkError PspNetworkSystem::Send(std::span<const std::uint8_t> data,
                                               runtime::NetworkDelivery delivery) {
    if (!room_) return runtime::NetworkError::NotInitialized;
    if (data.size() > net::Room::kMaxMessage) return runtime::NetworkError::InvalidArgument;

    const net::Delivery net_delivery =
        delivery == runtime::NetworkDelivery::Reliable
            ? net::Delivery::Reliable
            : net::Delivery::Unreliable;
    return room_->send(net::kEveryone, net_delivery, data)
        ? runtime::NetworkError::None
        : runtime::NetworkError::SendFailed;
}

runtime::NetworkPacket PspNetworkSystem::Receive() {
    runtime::NetworkPacket packet;
    if (!room_) return packet;

    const auto message = room_->receive();
    if (!message) return packet;
    packet.from = message->from;
    packet.delivery =
        message->delivery == net::Delivery::Reliable
            ? runtime::NetworkDelivery::Reliable
            : runtime::NetworkDelivery::Unreliable;
    packet.data = std::move(message->data);
    return packet;
}

} // namespace mhf::psp::adapters
