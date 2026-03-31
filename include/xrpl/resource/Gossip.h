#pragma once

#include <xrpl/beast/net/IPEndpoint.h>

#include <vector>

namespace xrpl {
namespace Resource {

/** Data format for exchanging consumption information across peers. */
struct Gossip
{
    explicit Gossip() = default;

    /** Describes a single consumer. */
    struct Item
    {
        explicit Item() = default;

        int balance;
        beast::IP::Endpoint address;
    };

    std::vector<Item> items;
};

}  // namespace Resource
}  // namespace xrpl
