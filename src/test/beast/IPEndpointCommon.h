#include <xrpl/basics/random.h>
#include <xrpl/beast/net/IPEndpoint.h>

namespace beast {
namespace IP {

inline Endpoint
randomEP(bool v4 = true)
{
    using namespace ripple;
    auto dv4 = []() -> AddressV4::bytes_type {
        return {
            {static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX))}};
    };
    auto dv6 = []() -> AddressV6::bytes_type {
        return {
            {static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
             static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX))}};
    };
    return Endpoint{
        v4 ? Address{AddressV4{dv4()}} : Address{AddressV6{dv6()}},
        rand_int<std::uint16_t>(1, UINT16_MAX)};
}

}  // namespace IP
}  // namespace beast
