#pragma once

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/resource/detail/Kind.h>

#include <utility>

namespace xrpl::Resource {

// The consumer key
struct Key
{
    Kind kind;
    beast::IP::Endpoint address;

    Key() = delete;

    Key(Kind k, beast::IP::Endpoint addr) : kind(k), address(std::move(addr))
    {
    }

    struct Hasher
    {
        std::size_t
        operator()(Key const& v) const
        {
            return addrHash_(v.address);
        }

    private:
        beast::Uhash<> addrHash_;
    };

    struct KeyEqual
    {
        KeyEqual() = default;

        bool
        operator()(Key const& lhs, Key const& rhs) const
        {
            return lhs.kind == rhs.kind && lhs.address == rhs.address;
        }

    private:
    };
};

}  // namespace xrpl::Resource
