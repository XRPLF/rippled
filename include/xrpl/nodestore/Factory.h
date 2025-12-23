#ifndef XRPL_NODESTORE_FACTORY_H_INCLUDED
#define XRPL_NODESTORE_FACTORY_H_INCLUDED

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Scheduler.h>

#include <nudb/store.hpp>

namespace ripple {

namespace NodeStore {

/** Base class for backend factories. */
class Factory
{
public:
    virtual ~Factory() = default;

    /** Retrieve the name of this factory. */
    virtual std::string
    getName() const = 0;

    /** Create an instance of this factory's backend.

        @param keyBytes The fixed number of bytes per key.
        @param parameters A set of key/value configuration pairs.
        @param burstSize Backend burst size in bytes.
        @param scheduler The scheduler to use for running tasks.
        @return A pointer to the Backend object.
    */
    virtual std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) = 0;

    /** Create an instance of this factory's backend.

        @param keyBytes The fixed number of bytes per key.
        @param parameters A set of key/value configuration pairs.
        @param burstSize Backend burst size in bytes.
        @param scheduler The scheduler to use for running tasks.
        @param context The context used by database.
        @return A pointer to the Backend object.
    */
    virtual std::unique_ptr<Backend>
    createInstance(
        size_t keyBytes,
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        nudb::context& context,
        beast::Journal journal)
    {
        return {};
    }
};

}  // namespace NodeStore
}  // namespace ripple

#endif
