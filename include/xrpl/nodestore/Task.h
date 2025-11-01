#ifndef XRPL_NODESTORE_TASK_H_INCLUDED
#define XRPL_NODESTORE_TASK_H_INCLUDED

namespace ripple {
namespace NodeStore {

/** Derived classes perform scheduled tasks. */
struct Task
{
    virtual ~Task() = default;

    /** Performs the task.
        The call may take place on a foreign thread.
    */
    virtual void
    performScheduledTask() = 0;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
