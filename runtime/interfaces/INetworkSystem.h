#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mhf::runtime {

enum class NetworkDelivery : std::uint8_t {
    Unreliable,
    Reliable,
};

enum class NetworkError : std::uint8_t {
    None,
    NotInitialized,
    NotConnected,
    InvalidArgument,
    SendFailed,
    ReceiveFailed,
};

struct NetworkPacket {
    std::uint32_t from = 0;
    NetworkDelivery delivery = NetworkDelivery::Unreliable;
    std::vector<std::uint8_t> data;
};

class INetworkSystem {
public:
    virtual ~INetworkSystem() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() noexcept = 0;
    virtual NetworkError Send(std::span<const std::uint8_t> data, NetworkDelivery delivery) = 0;
    virtual NetworkPacket Receive() = 0;
};

} // namespace mhf::runtime
