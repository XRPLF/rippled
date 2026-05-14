/**
 * @file SeqProxy.h
 * @brief Unified sequence/ticket identifier for XRPL transactions.
 */

#pragma once

#include <cstdint>
#include <ostream>

namespace xrpl {

/** A type-tagged @c uint32_t that identifies a transaction by either a
 *  traditional account sequence number or a ticket sequence number.
 *
 *  Before the Tickets feature, every XRPL transaction consumed exactly one
 *  account sequence number in order, so a plain @c uint32_t was sufficient.
 *  Tickets allow an account to pre-reserve sequence slots and use them
 *  out-of-order, which introduces a second namespace of transaction
 *  identifiers.  @c SeqProxy encapsulates the choice in one place so callers
 *  never need to carry a separate @c bool isTicket flag.
 *
 *  The raw @c value() is used as a ledger-object key for Offers, Checks,
 *  Payment Channels, and Escrows — the same role a bare sequence number
 *  played before tickets existed.  This is safe because of two invariants
 *  maintained by the @c TicketCreate transactor:
 *
 *  1. Every ticket created has a numeric value that falls within the range
 *     the account root's sequence has already advanced past — so a ticket
 *     value can never equal any sequence number that will be consumed in the
 *     future by that account.
 *  2. When a batch of tickets is created, the account root's sequence is
 *     advanced to one past the highest ticket number in the batch, permanently
 *     retiring all of those values from the sequence namespace.
 *
 *  Together these guarantee that ticket values and sequence values for a
 *  given account never collide, even when stored without type metadata.
 *
 *  @note The sort order imposed by @c operator< places all sequence-typed
 *      proxies strictly before all ticket-typed proxies, regardless of
 *      numeric value.  @c CanonicalTXSet relies on this to ensure that
 *      @c TicketCreate transactions (which carry a sequence number) always
 *      precede the ticket-consuming transactions they enable during consensus
 *      replay.
 *
 *  @see STTx::getSeqProxy()  — primary production construction site
 *  @see CanonicalTXSet       — uses SeqProxy as the per-account sort key
 *  @see Indexes::ticketIndex() — uses SeqProxy to derive the ledger-object key
 */
class SeqProxy
{
public:
    /** Discriminator indicating whether the proxy holds a sequence or ticket. */
    enum class Type : std::uint8_t {
        Seq = 0,  ///< Traditional account sequence number.
        Ticket    ///< Ticket sequence number (out-of-order slot).
    };

private:
    std::uint32_t value_;
    Type type_;

public:
    /** Construct a SeqProxy with an explicit type and value.
     *
     *  Prefer the @c sequence() factory for the common case.  Ticket proxies
     *  are typically constructed directly: @c SeqProxy{SeqProxy::Type::Ticket, v}.
     *
     *  @param t  Whether this proxy represents a sequence or a ticket.
     *  @param v  The numeric value of the sequence or ticket.
     */
    constexpr explicit SeqProxy(Type t, std::uint32_t v) : value_{v}, type_{t}
    {
    }

    SeqProxy(SeqProxy const& other) = default;

    SeqProxy&
    operator=(SeqProxy const& other) = default;

    /** Create a sequence-typed SeqProxy.
     *
     *  Named factory for the common case.  Ticket construction uses the
     *  explicit constructor directly, making it visibly intentional at each
     *  call site.
     *
     *  @param v  The account sequence number.
     *  @return   A SeqProxy of type @c Type::Seq with value @c v.
     */
    static constexpr SeqProxy
    sequence(std::uint32_t v)
    {
        return SeqProxy{Type::Seq, v};
    }

    /** Return the raw numeric value of this proxy.
     *
     *  Used as a ledger-object key for Offers, Checks, Payment Channels, and
     *  Escrows.  Safe to use without the type tag because the TicketCreate
     *  invariants guarantee no numeric collision between sequence and ticket
     *  values for the same account (see class-level documentation).
     *
     *  @return The @c uint32_t sequence or ticket number.
     */
    [[nodiscard]] constexpr std::uint32_t
    value() const
    {
        return value_;
    }

    /** Return @c true if this proxy holds a traditional sequence number. */
    [[nodiscard]] constexpr bool
    isSeq() const
    {
        return type_ == Type::Seq;
    }

    /** Return @c true if this proxy holds a ticket sequence number. */
    [[nodiscard]] constexpr bool
    isTicket() const
    {
        return type_ == Type::Ticket;
    }

    /** Increment the proxy's value in place and return @c *this.
     *
     *  A named method rather than @c operator+= is deliberate: incrementing
     *  a @c SeqProxy is an unusual operation (currently used only in tests to
     *  step through a sequence of dummy transactions) and the explicit name
     *  prevents accidental arithmetic on what is normally a fixed identifier.
     *
     *  @param amount  Number of positions to advance the value.
     *  @return        Reference to @c *this after the increment.
     */
    SeqProxy&
    advanceBy(std::uint32_t amount)
    {
        value_ += amount;
        return *this;
    }

    /** Test equality — two proxies are equal only if both type and value match.
     *
     *  A sequence proxy and a ticket proxy with the same numeric value are
     *  @b not equal.
     */
    friend constexpr bool
    operator==(SeqProxy lhs, SeqProxy rhs)
    {
        if (lhs.type_ != rhs.type_)
            return false;
        return (lhs.value() == rhs.value());
    }

    /** Test inequality. */
    friend constexpr bool
    operator!=(SeqProxy lhs, SeqProxy rhs)
    {
        return !(lhs == rhs);
    }

    /** Less-than comparison with type-first ordering.
     *
     *  All sequence-typed proxies sort strictly before all ticket-typed
     *  proxies, regardless of numeric value.  Within the same type, proxies
     *  are ordered numerically.  This means even the largest possible sequence
     *  number (@c UINT32_MAX) sorts before the smallest ticket (@c 0).
     *
     *  @note @c CanonicalTXSet depends on this invariant: it ensures that
     *      @c TicketCreate transactions (sequence-based) always precede the
     *      ticket-consuming transactions they enable in consensus ordering.
     */
    friend constexpr bool
    operator<(SeqProxy lhs, SeqProxy rhs)
    {
        if (lhs.type_ != rhs.type_)
            return lhs.type_ < rhs.type_;
        return lhs.value() < rhs.value();
    }

    /** Greater-than comparison. */
    friend constexpr bool
    operator>(SeqProxy lhs, SeqProxy rhs)
    {
        return rhs < lhs;
    }

    /** Greater-than-or-equal comparison. */
    friend constexpr bool
    operator>=(SeqProxy lhs, SeqProxy rhs)
    {
        return !(lhs < rhs);
    }

    /** Less-than-or-equal comparison. */
    friend constexpr bool
    operator<=(SeqProxy lhs, SeqProxy rhs)
    {
        return !(lhs > rhs);
    }

    /** Stream a human-readable representation: @c "sequence N" or @c "ticket N". */
    friend std::ostream&
    operator<<(std::ostream& os, SeqProxy seqProx)
    {
        os << (seqProx.isSeq() ? "sequence " : "ticket ");
        os << seqProx.value();
        return os;
    }
};
}  // namespace xrpl
