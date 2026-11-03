#pragma once

namespace xrpl::node_store {

/**
 * Derived classes perform scheduled tasks.
 */
struct Task
{
    virtual ~Task() = default;

    /**
     * Performs the task.
     * The call may take place on a foreign thread.
     */
    virtual void
    performScheduledTask() = 0;
};

}  // namespace xrpl::node_store
