#pragma once

#include <utility>

namespace xrpl::protocol_autogen {

/**
 * A wrapper that owns an object and provides access through a typed wrapper.
 *
 * This is useful when you want to hold both the raw underlying object (like SLE or STTx)
 * and its type-safe wrapper together with proper ownership semantics.
 *
 * The class is non-copyable and non-movable because the wrapper holds a reference
 * to the owned object.
 *
 * @tparam TObject The underlying object type (e.g., SLE, STTx)
 * @tparam TWrapper The typed wrapper class (e.g., AccountRoot, Payment)
 *
 * Example usage:
 * @code
 *     // From a builder
 *     auto owned = accountRootBuilder.buildOwning(index);
 *     owned->getBalance();      // Access via typed wrapper
 *     owned.object();           // Access raw SLE
 *
 *     // Direct construction
 *     Owning<SLE, AccountRoot> owned{std::move(sle)};
 * @endcode
 */
template <typename TObject, typename TWrapper>
class Owning
{
public:
    /**
     * Construct by taking ownership of the object.
     * @param obj The object to own (will be moved from)
     */
    explicit Owning(TObject obj) : object_(std::move(obj)), wrapper_(object_)
    {
    }

    // Non-copyable and non-movable (wrapper holds reference to object_)
    Owning(Owning const&) = delete;
    Owning&
    operator=(Owning const&) = delete;
    Owning(Owning&&) = delete;
    Owning&
    operator=(Owning&&) = delete;

    /**
     * Access the typed wrapper.
     */
    [[nodiscard]]
    TWrapper&
    get()
    {
        return wrapper_;
    }

    [[nodiscard]]
    TWrapper const&
    get() const
    {
        return wrapper_;
    }

    /**
     * Dereference to access the wrapper.
     */
    [[nodiscard]]
    TWrapper&
    operator*()
    {
        return wrapper_;
    }

    [[nodiscard]]
    TWrapper const&
    operator*() const
    {
        return wrapper_;
    }

    /**
     * Arrow operator for convenient wrapper access.
     */
    [[nodiscard]]
    TWrapper*
    operator->()
    {
        return &wrapper_;
    }

    [[nodiscard]]
    TWrapper const*
    operator->() const
    {
        return &wrapper_;
    }

    /**
     * Access the underlying object directly.
     */
    [[nodiscard]]
    TObject&
    object()
    {
        return object_;
    }

    [[nodiscard]]
    TObject const&
    object() const
    {
        return object_;
    }

private:
    TObject object_;    // Must be declared first (initialised first)
    TWrapper wrapper_;  // Holds reference to object_
};

}  // namespace xrpl::protocol_autogen
