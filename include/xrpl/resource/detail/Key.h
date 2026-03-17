#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/resource/detail/Kind.h>

namespace xrpl {
namespace Resource {

// The consumer key
struct Key
{
    Kind kind;
    beast::IP::Endpoint address;

    Key() = delete;

    Key(Kind k, beast::IP::Endpoint const& addr) : kind(k), address(addr)
    {
    }

    struct hasher
    {
        std::size_t
        operator()(Key const& v) const
        {
            return addr_hash_(v.address);
        }

    private:
        beast::uhash<> addr_hash_;
    };

    struct key_equal
    {
        key_equal() = default;

        bool
        operator()(Key const& lhs, Key const& rhs) const
        {
            return lhs.kind == rhs.kind && lhs.address == rhs.address;
        }

    private:
    };
};

}  // namespace Resource
}  // namespace xrpl
