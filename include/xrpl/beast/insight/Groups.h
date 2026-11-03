#pragma once

#include <xrpl/beast/insight/Collector.h>
#include <xrpl/beast/insight/Group.h>

#include <memory>
#include <string>

namespace beast::insight {

/**
 * A container for managing a set of metric groups.
 */
class Groups
{
public:
    virtual ~Groups() = 0;

    /**
     * Find or create a new collector with a given name.
     */
    /** @{ */
    virtual Group::Ptr const&
    get(std::string const& name) = 0;

    Group::Ptr const&
    operator[](std::string const& name)
    {
        return get(name);
    }
    /** @} */
};

/**
 * Create a group container that uses the specified collector.
 */
std::unique_ptr<Groups>
makeGroups(Collector::Ptr const& collector);

}  // namespace beast::insight
