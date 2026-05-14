/** @file
 *  Defines the `Book` type — the identity of an XRPL DEX order book — together
 *  with `std::hash` and `boost::hash` specializations for `Issue`, `MPTIssue`,
 *  `Asset`, and `Book` needed by unordered containers throughout the codebase.
 */

#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/Asset.h>

#include <boost/utility/base_from_member.hpp>

namespace xrpl {

/** Identity of an XRPL order book: a directed pair of assets.
 *
 *  An order book is the set of all open offers to exchange one asset for
 *  another in a specific direction.  `in` is the asset a taker spends;
 *  `out` is the asset a taker receives.  Because `Asset` is a variant of
 *  `Issue` and `MPTIssue`, a `Book` can represent any combination of XRP,
 *  IOU, and MPT asset classes.
 *
 *  When `domain` is set, the book is scoped to a `PermissionedDomain`
 *  ledger entry identified by that `uint256` index.  A domain-scoped book
 *  and the corresponding open book are distinct even when their `in`/`out`
 *  assets are identical — equality, ordering, and hashing all include
 *  `domain`.
 *
 *  Inherits `CountedObject<Book>` for diagnostic instance counting only;
 *  this has no effect on protocol logic.
 *
 *  @invariant A well-formed book satisfies `isConsistent(*this)`:
 *      both legs are individually consistent and `in != out`.
 *  @see isConsistent, reversed
 */
class Book final : public CountedObject<Book>
{
public:
    /** The asset being spent by the taker (offered). */
    Asset in;

    /** The asset being received by the taker (wanted). */
    Asset out;

    /** Optional permissioned-domain scope for this order book.
     *
     *  When present, the `uint256` is the ledger index of a
     *  `PermissionedDomain` object that gates participation.  Absent means
     *  the book is open to all accounts.
     */
    std::optional<uint256> domain;

    Book() = default;

    /** Construct a Book from explicit asset legs and an optional domain.
     *
     *  @param in     Asset being spent by the taker.
     *  @param out    Asset being received by the taker.
     *  @param domain Ledger index of the `PermissionedDomain` that scopes
     *      this book, or `std::nullopt` for an open book.
     */
    Book(Asset const& in, Asset const& out, std::optional<uint256> const& domain)
        : in(in), out(out), domain(domain)
    {
    }
};

/** Check that a Book is self-consistent.
 *
 *  A book is consistent when both `in` and `out` are individually consistent
 *  (e.g., no XRP currency paired with a non-XRP issuer) and `in != out`.
 *  A book with identical legs would represent trading an asset against itself
 *  and would cause infinite-loop offer matching.
 *
 *  @note `book.domain` is not validated here; semantic validity of the domain
 *      identifier belongs to higher-level transaction processing.
 *  @param book The order book to validate.
 *  @return `true` if both legs are individually consistent and `in != out`.
 */
bool
isConsistent(Book const& book);

/** Format a Book as a human-readable string for logging and diagnostics.
 *
 *  Produces a string of the form `"<in>-><out>"` using the `to_string`
 *  representations of each asset leg.  The arrow makes directionality
 *  explicit.  This format is not part of the wire protocol.
 *
 *  @param book The order book to convert.
 *  @return A string of the form `"<in>-><out>"`.
 */
std::string
to_string(Book const& book);

/** Write a Book to an output stream in diagnostic form.
 *
 *  Delegates to `to_string(book)`.
 *
 *  @param os The output stream to write to.
 *  @param x  The order book to write.
 *  @return `os`, to allow chaining.
 */
std::ostream&
operator<<(std::ostream& os, Book const& x);

/** Append a Book to a cryptographic hash state (beast hash pipeline).
 *
 *  Hashes `in` and `out` unconditionally, then appends `domain` only when
 *  present.  A book with a domain and one without — even with identical
 *  asset legs — therefore produce different digests.  This matters for
 *  ledger index derivation in `Indexes.cpp`, where the presence of a domain
 *  conditionally changes the hash inputs used to locate the book's offer
 *  directory.
 *
 *  @tparam Hasher A beast-compatible hash accumulator.
 *  @param h The hash state to append to.
 *  @param b The order book whose fields are appended.
 */
template <class Hasher>
void
hash_append(Hasher& h, Book const& b)
{
    using beast::hash_append;
    hash_append(h, b.in, b.out);
    if (b.domain)
        hash_append(h, *(b.domain));
}

/** Return the mirror-image order book with `in` and `out` swapped.
 *
 *  Preserves `domain` unchanged — a domain-scoped market is the same market
 *  when viewed from either direction.  Used by the Subscribe/Unsubscribe RPC
 *  handlers when a client requests the `both` flag so that updates from both
 *  the bid and ask sides are delivered.
 *
 *  @param book The order book to reverse.
 *  @return A new `Book` with `in` and `out` exchanged and `domain` unchanged.
 */
Book
reversed(Book const& book);

/** @{ */
/** Test two Books for equality.
 *
 *  Two books are equal only when `in`, `out`, and `domain` all compare equal.
 *  A domain-scoped book is never equal to an open book with the same asset
 *  legs.
 */
[[nodiscard]] constexpr bool
operator==(Book const& lhs, Book const& rhs)
{
    return (lhs.in == rhs.in) && (lhs.out == rhs.out) && (lhs.domain == rhs.domain);
}
/** @} */

/** @{ */
/** Three-way comparison establishing a strict weak ordering over Books.
 *
 *  Orders first by `in`, then by `out`, then by `domain`.  An absent domain
 *  compares less than any present domain.  This ordering is used by sorted
 *  containers such as subscription routing tables and by `BookDirs` traversal
 *  to iterate over offer directories deterministically.
 *
 *  @note The optional comparison is performed manually (rather than relying
 *      on the standard library's spaceship support for `optional`) to
 *      guarantee a `std::weak_ordering` return type consistent with the
 *      `Asset` spaceship result.
 */
[[nodiscard]] constexpr std::weak_ordering
operator<=>(Book const& lhs, Book const& rhs)
{
    if (auto const c{lhs.in <=> rhs.in}; c != 0)
        return c;
    if (auto const c{lhs.out <=> rhs.out}; c != 0)
        return c;

    // Manually compare optionals: absent domain sorts before any present domain.
    if (lhs.domain && rhs.domain)
        return *lhs.domain <=> *rhs.domain;
    if (!lhs.domain && rhs.domain)
        return std::weak_ordering::less;
    if (lhs.domain && !rhs.domain)
        return std::weak_ordering::greater;

    return std::weak_ordering::equivalent;
}
/** @} */

}  // namespace xrpl

//------------------------------------------------------------------------------

namespace std {

/** `std::hash` specialization for `xrpl::Issue`.
 *
 *  Combines the currency hash with the account (issuer) hash via
 *  `boost::hash_combine`.  For XRP, the issuer field is ignored because
 *  all XRP issuers are equivalent by protocol definition, ensuring that
 *  all representations of XRP hash identically.
 */
template <>
struct hash<xrpl::Issue> : private boost::base_from_member<std::hash<xrpl::Currency>, 0>,
                           private boost::base_from_member<std::hash<xrpl::AccountID>, 1>
{
private:
    using currency_hash_type = boost::base_from_member<std::hash<xrpl::Currency>, 0>;
    using issuer_hash_type = boost::base_from_member<std::hash<xrpl::AccountID>, 1>;

public:
    hash() = default;

    using value_type = std::size_t;
    using argument_type = xrpl::Issue;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(currency_hash_type::member(value.currency));
        if (!isXRP(value.currency))
            boost::hash_combine(result, issuer_hash_type::member(value.account));
        return result;
    }
};

/** `std::hash` specialization for `xrpl::MPTIssue`.
 *
 *  Hashes only the 192-bit `MPTID` (32-bit sequence number concatenated with
 *  the 160-bit issuer account), which is the canonical unique identifier for
 *  an MPT issuance.
 */
template <>
struct hash<xrpl::MPTIssue> : private boost::base_from_member<std::hash<xrpl::MPTID>, 0>
{
private:
    using id_hash_type = boost::base_from_member<std::hash<xrpl::MPTID>, 0>;

public:
    explicit hash() = default;

    using value_type = std::size_t;
    using argument_type = xrpl::MPTIssue;

    value_type
    operator()(argument_type const& value) const
    {
        value_type const result(id_hash_type::member(value.getMptID()));
        return result;
    }
};

/** `std::hash` specialization for `xrpl::Asset`.
 *
 *  Visits the underlying variant and dispatches to the appropriate
 *  `std::hash<Issue>` or `std::hash<MPTIssue>` specialization.
 */
template <>
struct hash<xrpl::Asset>
{
private:
    using value_type = std::size_t;
    using argument_type = xrpl::Asset;

    using issue_hasher = std::hash<xrpl::Issue>;
    using mptissue_hasher = std::hash<xrpl::MPTIssue>;

    issue_hasher m_issue_hasher_;
    mptissue_hasher m_mptissue_hasher_;

public:
    explicit hash() = default;

    value_type
    operator()(argument_type const& asset) const
    {
        return asset.visit(
            [&](xrpl::Issue const& issue) {
                value_type const result(m_issue_hasher_(issue));
                return result;
            },
            [&](xrpl::MPTIssue const& issue) {
                value_type const result(m_mptissue_hasher_(issue));
                return result;
            });
    }
};

//------------------------------------------------------------------------------

/** `std::hash` specialization for `xrpl::Book`.
 *
 *  Seeds from `hash(in)`, combines `hash(out)` via `boost::hash_combine`,
 *  then conditionally combines `hash(domain)` when a domain is present.
 *  A domain-scoped book and an otherwise-identical open book therefore
 *  produce distinct hash values, which is required for correct keying in
 *  subscription routing tables and offer-directory indexes.
 */
template <>
struct hash<xrpl::Book>
{
private:
    using asset_hasher = std::hash<xrpl::Asset>;
    using uint256_hasher = xrpl::uint256::hasher;

    asset_hasher issue_hasher_;
    uint256_hasher uint256_hasher_;

public:
    hash() = default;

    using value_type = std::size_t;
    using argument_type = xrpl::Book;

    value_type
    operator()(argument_type const& value) const
    {
        value_type result(issue_hasher_(value.in));
        boost::hash_combine(result, issue_hasher_(value.out));

        if (value.domain)
            boost::hash_combine(result, uint256_hasher_(*value.domain));

        return result;
    }
};

}  // namespace std

//------------------------------------------------------------------------------

namespace boost {

/** `boost::hash` adapter for `xrpl::Issue`.
 *
 *  Inherits `std::hash<xrpl::Issue>` so that Boost.Unordered and
 *  Boost.MultiIndex containers resolve the same hash function as standard
 *  unordered containers, avoiding divergence between the two hash registries.
 *
 *  @note Constructor inheritance (`using Base::Base`) is omitted because it
 *      was broken in Visual Studio 2012; an explicit defaulted constructor
 *      is provided instead.
 */
template <>
struct hash<xrpl::Issue> : std::hash<xrpl::Issue>
{
    hash() = default;

    using Base = std::hash<xrpl::Issue>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

/** `boost::hash` adapter for `xrpl::MPTIssue`.
 *
 *  Delegates to `std::hash<xrpl::MPTIssue>` so that Boost containers use
 *  the same hash logic as standard containers.
 */
template <>
struct hash<xrpl::MPTIssue> : std::hash<xrpl::MPTIssue>
{
    explicit hash() = default;

    using Base = std::hash<xrpl::MPTIssue>;
};

/** `boost::hash` adapter for `xrpl::Asset`.
 *
 *  Delegates to `std::hash<xrpl::Asset>` so that Boost containers use the
 *  same variant-dispatching hash logic as standard containers.
 */
template <>
struct hash<xrpl::Asset> : std::hash<xrpl::Asset>
{
    explicit hash() = default;

    using Base = std::hash<xrpl::Asset>;
};

/** `boost::hash` adapter for `xrpl::Book`.
 *
 *  Delegates to `std::hash<xrpl::Book>` so that Boost containers use the
 *  same domain-aware hash logic as standard containers.
 *
 *  @note Constructor inheritance (`using Base::Base`) is omitted because it
 *      was broken in Visual Studio 2012; an explicit defaulted constructor
 *      is provided instead.
 */
template <>
struct hash<xrpl::Book> : std::hash<xrpl::Book>
{
    hash() = default;

    using Base = std::hash<xrpl::Book>;
    // VFALCO NOTE broken in vs2012
    // using Base::Base; // inherit ctors
};

}  // namespace boost
