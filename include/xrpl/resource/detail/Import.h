#ifndef XRPL_RESOURCE_IMPORT_H_INCLUDED
#define XRPL_RESOURCE_IMPORT_H_INCLUDED

#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/detail/Entry.h>

namespace ripple {
namespace Resource {

/** A set of imported consumer data from a gossip origin. */
struct Import
{
    struct Item
    {
        explicit Item() = default;

        int balance;
        Consumer consumer;
    };

    // Dummy argument required for zero-copy construction
    Import(int = 0) : whenExpires()
    {
    }

    // When the imported data expires
    clock_type::time_point whenExpires;

    // List of remote entries
    std::vector<Item> items;
};

}  // namespace Resource
}  // namespace ripple

#endif
