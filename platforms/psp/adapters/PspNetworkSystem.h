#pragma once

#include "interfaces/INetworkSystem.h"

#include "net/local_network.hpp"
#include "net/room.hpp"

#include <memory>
#include <string>

namespace mhf::psp::adapters {

class PspNetworkSystem final : public runtime::INetworkSystem {
public:
    PspNetworkSystem();
    explicit PspNetworkSystem(std::string room_code);
    ~PspNetworkSystem() override;

    bool Initialize() override;
    void Shutdown() noexcept override;
    runtime::NetworkError Send(std::span<const std::uint8_t> data,
                               runtime::NetworkDelivery delivery) override;
    runtime::NetworkPacket Receive() override;

private:
    net::LocalNetwork network_;
    std::unique_ptr<net::Room> room_;
    std::string room_code_;
};

} // namespace mhf::psp::adapters
