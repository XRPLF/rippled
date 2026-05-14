#pragma once

#include <xrpl/protocol/Concepts.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/XRPAmount.h>

namespace xrpl {

/**
 * Type-erased amount wrapper for the payment path engine.
 *
 * The `Step` abstraction uses virtual dispatch (`rev`/`fwd`) yet each
 * concrete step works with a single, statically-typed amount —
 * `XRPAmount`, `IOUAmount`, or `MPTAmount`. `EitherAmount` bridges this
 * gap by holding `std::variant<XRPAmount, IOUAmount, MPTAmount>` behind a
 * uniform value type that flows freely through the virtual step interface.
 *
 * Value semantics (no heap allocation, copyable, storable in `std::vector`)
 * make this suitable for use in `FlowDebugInfo` diagnostic vectors and as
 * return values from `Step::cachedIn()`/`cachedOut()`. A pointer-based
 * design would require ownership management for what is fundamentally a
 * lightweight numeric wrapper.
 *
 * All templated members are constrained by the `StepAmount` concept, which
 * explicitly enumerates the three valid amount types. This closes the
 * variant: you cannot accidentally construct from `STAmount` or any other
 * compatible numeric type.
 *
 * @see Step::rev, Step::fwd, Step::cachedIn, Step::cachedOut
 */
struct EitherAmount
{
    /** The underlying variant holding the active amount. */
    std::variant<XRPAmount, IOUAmount, MPTAmount> amount;

    /** Constructs a default (value-initialized) `EitherAmount`. */
    explicit EitherAmount() = default;

    /**
     * Constructs an `EitherAmount` holding the given typed amount.
     *
     * @tparam T One of `XRPAmount`, `IOUAmount`, or `MPTAmount`.
     * @param a The amount value to store.
     */
    template <StepAmount T>
    explicit EitherAmount(T const& a) : amount(a)
    {
    }

    /**
     * Returns true if the variant currently holds type `T`.
     *
     * Call this before `get<T>()` when the active type is not statically
     * guaranteed, to avoid the `std::logic_error` thrown on mismatch.
     *
     * @tparam T One of `XRPAmount`, `IOUAmount`, or `MPTAmount`.
     */
    template <StepAmount T>
    [[nodiscard]] bool
    holds() const
    {
        return std::holds_alternative<T>(amount);
    }

    /**
     * Returns a const reference to the held amount as type `T`.
     *
     * Throws `std::logic_error` if the variant does not hold `T`. This
     * fail-fast design signals a programming error in the flow engine
     * rather than a runtime data condition; mismatched access means the
     * step dispatch wired the wrong amount type.
     *
     * @tparam T One of `XRPAmount`, `IOUAmount`, or `MPTAmount`.
     * @return Const reference to the held amount.
     * @throws std::logic_error if `holds<T>()` is false.
     */
    template <StepAmount T>
    [[nodiscard]] T const&
    get() const
    {
        if (!holds<T>())
            Throw<std::logic_error>("EitherAmount doesn't hold requested amount");
        return std::get<T>(amount);
    }

#ifndef NDEBUG
    /**
     * Writes a human-readable representation of the held amount to `stream`.
     *
     * Only compiled in debug builds. Uses `std::visit` with a template lambda
     * to dispatch `to_string` to whichever concrete amount type is active,
     * avoiding dead formatting overhead on the production hot path.
     */
    friend std::ostream&
    operator<<(std::ostream& stream, EitherAmount const& amt)
    {
        std::visit([&]<StepAmount T>(T const& a) { stream << to_string(a); }, amt.amount);
        return stream;
    }
#endif
};

/**
 * Free-function accessor for `EitherAmount`; delegates to `amt.get<T>()`.
 *
 * Provides an alternative calling convention used throughout the flow engine
 * (e.g., `get<XRPAmount>(either)` instead of `either.get<XRPAmount>()`).
 * Throws `std::logic_error` via the member `get` if `T` is not the active type.
 *
 * @tparam T One of `XRPAmount`, `IOUAmount`, or `MPTAmount`.
 * @param amt The `EitherAmount` to extract from.
 * @return Const reference to the held amount.
 * @throws std::logic_error if `amt` does not hold type `T`.
 */
template <StepAmount T>
T const&
get(EitherAmount const& amt)
{
    return amt.get<T>();
}

}  // namespace xrpl
