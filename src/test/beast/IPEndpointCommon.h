//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/beast/net/IPEndpoint.h>
#include <ripple/basics/random.h>

namespace beast {
namespace IP {

inline Endpoint randomEP (bool v4 = true)
{
    using namespace ripple;
    auto dv4 = []() -> AddressV4::bytes_type {
        return {{
            static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
            static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
            static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
            static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX))
        }};
    };
    auto dv6 = []() -> AddressV6::bytes_type {
        return {{
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
            static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX)),
            static_cast<std::uint8_t>(rand_int<int>(1, UINT8_MAX))
        }};
    };
    return Endpoint {
        v4 ? Address { AddressV4 {dv4()} } : Address{ AddressV6 {dv6()} },
        rand_int<std::uint16_t>(1, UINT16_MAX)
    };
}

}
}
