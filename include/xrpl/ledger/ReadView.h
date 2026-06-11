#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/beast/hash/uhash.h>
#include <xrpl/ledger/detail/ReadViewFwdRange.h>
#include <xrpl/protocol/Fees.h>
#include <xrpl/protocol/IOUAmount.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerHeader.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>

#include <cstdint>
#include <optional>
#include <unordered_set>

namespace xrpl {

//------------------------------------------------------------------------------

/** A view into a ledger.

    This interface provides read access to state
    and transaction items. There is no checkpointing
    or calculation of metadata.
*/
class ReadView
{
public:
    using tx_type = std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>;

    using key_type = uint256;

    using mapped_type = SLE::const_pointer;

    struct SlesType : detail::ReadViewFwdRange<SLE::const_pointer>
    {
        explicit SlesType(ReadView const& view);
        [[nodiscard]] Iterator
        begin() const;
        [[nodiscard]] Iterator
        end() const;
        [[nodiscard]] Iterator
        upperBound(key_type const& key) const;
    };

    struct TxsType : detail::ReadViewFwdRange<tx_type>
    {
        explicit TxsType(ReadView const& view);
        [[nodiscard]] bool
        empty() const;
        [[nodiscard]] Iterator
        begin() const;
        [[nodiscard]] Iterator
        end() const;
    };

    virtual ~ReadView() = default;

    ReadView&
    operator=(ReadView&& other) = delete;
    ReadView&
    operator=(ReadView const& other) = delete;

    ReadView() : sles(*this), txs(*this)
    {
    }

    ReadView(ReadView const& other) : sles(*this), txs(*this)
    {
    }

    ReadView(ReadView&& other) : sles(*this), txs(*this)
    {
    }

    /** Returns information about the ledger. */
    [[nodiscard]] virtual LedgerHeader const&
    header() const = 0;

    /** Returns true if this reflects an open ledger. */
    [[nodiscard]] virtual bool
    open() const = 0;

    /** Returns the close time of the previous ledger. */
    [[nodiscard]] NetClock::time_point
    parentCloseTime() const
    {
        return header().parentCloseTime;
    }

    /** Returns the sequence number of the base ledger. */
    [[nodiscard]] LedgerIndex
    seq() const
    {
        return header().seq;
    }

    /** Returns the fees for the base ledger. */
    [[nodiscard]] virtual Fees const&
    fees() const = 0;

    /** Returns the tx processing rules. */
    [[nodiscard]] virtual Rules const&
    rules() const = 0;

    /** Determine if a state item exists.

        @note This can be more efficient than calling read.

        @return `true` if a SLE is associated with the
                specified key.
    */
    [[nodiscard]] virtual bool
    exists(Keylet const& k) const = 0;

    /** Return the key of the next state item.

        This returns the key of the first state item
        whose key is greater than the specified key. If
        no such key is present, std::nullopt is returned.

        If `last` is engaged, returns std::nullopt when
        the key returned would be outside the open
        interval (key, last).
    */
    [[nodiscard]] virtual std::optional<key_type>
    succ(key_type const& key, std::optional<key_type> const& last = std::nullopt) const = 0;

    /** Return the state item associated with a key.

        Effects:
            If the key exists, gives the caller ownership
            of the non-modifiable corresponding SLE.

        @note While the returned SLE is `const` from the
              perspective of the caller, it can be changed
              by other callers through raw operations.

        @return `nullptr` if the key is not present or
                if the type does not match.
    */
    [[nodiscard]] virtual SLE::const_pointer
    read(Keylet const& k) const = 0;

    // Accounts in a payment are not allowed to use assets acquired during that
    // payment. The PaymentSandbox tracks the debits, credits, and owner count
    // changes that accounts make during a payment. `balanceHookIOU` adjusts
    // balances so newly acquired assets are not counted toward the balance.
    // This is required to support PaymentSandbox.
    [[nodiscard]] virtual STAmount
    balanceHookIOU(AccountID const& account, AccountID const& issuer, STAmount const& amount) const
    {
        XRPL_ASSERT(amount.holds<Issue>(), "balanceHookIOU: amount is for Issue");

        return amount;
    }

    // balanceHookMPT adjusts balances so newly acquired assets are not counted
    // toward the balance.
    [[nodiscard]] virtual STAmount
    balanceHookMPT(AccountID const& account, MPTIssue const& issue, std::int64_t amount) const
    {
        return STAmount{issue, amount};
    }

    // An offer owned by an issuer and selling MPT is limited by the issuer's
    // funds available to issue, which are originally available funds less
    // already self sold MPT amounts (MPT sell offer). This hook is used
    // by issuerFundsToSelfIssue() function.
    [[nodiscard]] virtual STAmount
    balanceHookSelfIssueMPT(MPTIssue const& issue, std::int64_t amount) const
    {
        return STAmount{issue, amount};
    }

    // Accounts in a payment are not allowed to use assets acquired during that
    // payment. The PaymentSandbox tracks the debits, credits, and owner count
    // changes that accounts make during a payment. `ownerCountHook` adjusts the
    // ownerCount so it returns the max value of the ownerCount so far.
    // This is required to support PaymentSandbox.
    [[nodiscard]] virtual std::uint32_t
    ownerCountHook(AccountID const& account, std::uint32_t count) const
    {
        return count;
    }

    // used by the implementation
    [[nodiscard]] virtual std::unique_ptr<SlesType::iter_base>
    slesBegin() const = 0;

    // used by the implementation
    [[nodiscard]] virtual std::unique_ptr<SlesType::iter_base>
    slesEnd() const = 0;

    // used by the implementation
    [[nodiscard]] virtual std::unique_ptr<SlesType::iter_base>
    slesUpperBound(key_type const& key) const = 0;

    // used by the implementation
    [[nodiscard]] virtual std::unique_ptr<TxsType::iter_base>
    txsBegin() const = 0;

    // used by the implementation
    [[nodiscard]] virtual std::unique_ptr<TxsType::iter_base>
    txsEnd() const = 0;

    /** Returns `true` if a tx exists in the tx map.

        A tx exists in the map if it is part of the
        base ledger, or if it is a newly inserted tx.
    */
    [[nodiscard]] virtual bool
    txExists(key_type const& key) const = 0;

    /** Read a transaction from the tx map.

        If the view represents an open ledger,
        the metadata object will be empty.

        @return A pair of nullptr if the
                key is not found in the tx map.
    */
    [[nodiscard]] virtual tx_type
    txRead(key_type const& key) const = 0;

    //
    // Memberspaces
    //

    /** Iterable range of ledger state items.

        @note Visiting each state entry in the ledger can
              become quite expensive as the ledger grows.
    */
    SlesType sles;

    // The range of transactions
    TxsType txs;
};

//------------------------------------------------------------------------------

/** ReadView that associates keys with digests. */
class DigestAwareReadView : public ReadView
{
public:
    using digest_type = uint256;

    DigestAwareReadView() = default;
    DigestAwareReadView(DigestAwareReadView const&) = default;

    /** Return the digest associated with the key.

        @return std::nullopt if the item does not exist.
    */
    [[nodiscard]] virtual std::optional<digest_type>
    digest(key_type const& key) const = 0;
};

//------------------------------------------------------------------------------

Rules
makeRulesGivenLedger(DigestAwareReadView const& ledger, Rules const& current);

Rules
makeRulesGivenLedger(
    DigestAwareReadView const& ledger,
    std::unordered_set<uint256, beast::Uhash<>> const& presets);

}  // namespace xrpl

#include <xrpl/ledger/detail/ReadViewFwdRange.ipp>
