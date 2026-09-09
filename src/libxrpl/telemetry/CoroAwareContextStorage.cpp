/**
 * Implementation of CoroAwareContextStorage.
 *
 * Mirrors opentelemetry's ThreadLocalContextStorage stack semantics, but the
 * stack lives in an xrpl::LocalValue so it follows a JobQueue::Coro across
 * yield/resume. All OpenTelemetry types stay confined to this translation unit.
 *
 * @see CoroAwareContextStorage (CoroAwareContextStorage.h)
 */

#ifdef XRPL_ENABLE_TELEMETRY

#include <xrpl/telemetry/CoroAwareContextStorage.h>

#include <xrpl/basics/LocalValue.h>

#include <opentelemetry/context/context.h>
#include <opentelemetry/context/runtime_context.h>
#include <opentelemetry/nostd/unique_ptr.h>

namespace xrpl::telemetry {

opentelemetry::context::Context
CoroAwareContextStorage::GetCurrent() noexcept
{
    // Hot path: Log.cpp reads the current context on EVERY log line on EVERY
    // thread. Peek the raw per-thread/coro store WITHOUT materializing it:
    // getLocalValues().get() returns nullptr when this thread has no store yet
    // and never allocates. Dereferencing stack_ (a LocalValue) would instead
    // heap-allocate the store + a map entry + the vector on first touch, so we
    // avoid that on the no-span path.
    //
    // Behaviour is identical to touching *stack_: a thread with no store has no
    // active span, and a store whose stack was never pushed yields an empty
    // vector — both cases map to an empty Context, matching stock
    // ThreadLocalContextStorage (Top() returns Context() when the stack is
    // empty). Only when a store already exists do we read the top frame.
    if (detail::getLocalValues().get() == nullptr)
        return opentelemetry::context::Context{};
    auto& stack = *stack_;
    return stack.empty() ? opentelemetry::context::Context{} : stack.back();
}

opentelemetry::nostd::unique_ptr<opentelemetry::context::Token>
CoroAwareContextStorage::Attach(opentelemetry::context::Context const& context) noexcept
{
    stack_->push_back(context);
    return CreateToken(context);
}

bool
CoroAwareContextStorage::Detach(opentelemetry::context::Token& token) noexcept
{
    auto& stack = *stack_;
    // Fast path: the token's frame is on top (the common LIFO case).
    if (!stack.empty() && token == stack.back())
    {
        stack.pop_back();
        return true;
    }
    // Fallback: token not on top — verify it is present, then pop down to and
    // including it (mirrors ThreadLocalContextStorage::Detach; also detaches
    // any child frames left above it).
    bool found = false;
    for (auto const& frame : stack)
    {
        if (token == frame)
        {
            found = true;
            break;
        }
    }
    if (!found)
        return false;
    while (!stack.empty() && !(token == stack.back()))
        stack.pop_back();
    if (!stack.empty())
        stack.pop_back();
    return true;
}

}  // namespace xrpl::telemetry

#endif  // XRPL_ENABLE_TELEMETRY
