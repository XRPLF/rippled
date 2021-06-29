//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#ifndef RIPPLE_PEERFINDER_NETGROUP_H_INCLUDED
#define RIPPLE_PEERFINDER_NETGROUP_H_INCLUDED

#include <ripple/beast/net/IPEndpoint.h>

namespace ripple {

class NetGroup
{
    enum Network {
        Unroutable = 0,
        IPv4 = 1,
        IPv6 = 2,
        NetOnion = 3,
        NetInternal = 4,
        NetMax = 5
    };

private:
    using bytes_type = boost::asio::ip::address_v6::bytes_type;
    bytes_type rawBytes_;
    Network network_{Network::IPv6};
    bool const isV4_{false};
    bool const isLoopback_{false};

public:
    NetGroup(beast::IP::Endpoint const&);
    ~NetGroup() = default;
    std::uint64_t
    calculateKeyedNetGroup() const;

private:
    std::vector<u_char>
    getGroup() const;
    std::uint32_t
    getByte(int n) const
    {
        return rawBytes_[15 - n];
    }
    std::uint32_t
    getNetClass() const;
    bool
    isLocal() const;
    bool
    isInternal() const
    {
        return network_ == Network::NetInternal;
    }
    bool
    isRoutable() const;
    bool
    isIPv4() const
    {
        return isV4_;
    }
    bool
    isIPv6() const
    {
        return !isV4_;
    }
    bool
    isRFC1918() const;
    bool
    isRFC2544() const;
    bool
    isRFC3927() const;
    bool
    isRFC4862() const;
    bool
    isRFC6598() const;
    bool
    isRFC5737() const;
    bool
    isRFC4193() const;
    bool
    isRFC4843() const;
    bool
    isRFC7343() const;
    bool
    isRFC3849() const;
    bool
    isRFC3964() const;
    bool
    isRFC6052() const;
    bool
    isRFC4380() const;
    bool
    isRFC6145() const;
    bool
    isTor() const
    {
        return network_ == Network::NetOnion;
    }
    bool
    hasLinkedIPv4() const;
    std::uint32_t
    getLinkedIPv4() const;
    bool
    isHeNet() const;
};

}  // namespace ripple
#endif  // RIPPLE_PEERFINDER_NETGROUP_H_INCLUDED
