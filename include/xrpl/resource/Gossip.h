#ifndef XRPL_RESOURCE_GOSSIP_H_INCLUDED
#define XRPL_RESOURCE_GOSSIP_H_INCLUDED

#include <xrpl/beast/net/IPEndpoint.h>

#include <vector>

namespace ripple {
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
}  // namespace ripple

#endif
