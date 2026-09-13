#pragma once

#include <xrpl/beast/net/IPEndpoint.h>

#include <vector>

namespace xrpl::resource {

/**
 * Data format for exchanging consumption information across peers.
 */
struct Gossip
{
    explicit Gossip() = default;

    /**
     * Describes a single consumer.
     */
    struct Item
    {
        explicit Item() = default;

        int balance{};
        beast::ip::Endpoint address;
    };

    std::vector<Item> items;
};

}  // namespace xrpl::resource
