#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/protocol/RippleLedgerHash.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>

namespace xrpl {

/** Ordered transaction queue for deterministic consensus application.
 *
 *  Holds transactions deferred from a previous ledger-building pass and
 *  re-applies them in the next pass. The "canonical" in the name is the
 *  ordering guarantee: given the same input transaction set and the same
 *  salt, every validator iterates and applies transactions in identical
 *  sequence, which is required for Byzantine fault-tolerant ledger
 *  agreement.
 *
 *  Ordering is three-level (implemented in `Key::operator<`):
 *  1. Salted account ID — groups all transactions from the same account.
 *  2. `SeqProxy` — within an account, sequence-based transactions sort
 *     before ticket-based ones, preserving the dependency that a ticket
 *     creator must apply before ticket consumers.
 *  3. Transaction ID — tiebreaker within the same account and sequence.
 *
 *  @note Account keys are XORed with a `LedgerHash` salt at construction
 *      (and via `reset()`) so that no actor can mine account addresses to
 *      achieve a persistent early-sort advantage across ledger rounds.
 *
 *  @note Inherits from `CountedObject<CanonicalTXSet>` for diagnostic
 *      memory-pressure accounting; the instance count is queryable via
 *      `CountedObjects::getInstance().getCounts()` and has no effect on
 *      behavior.
 *
 *  Usage in `BuildLedger.cpp`: `applyTransactions()` iterates this set in
 *  map order across multiple passes, erasing each transaction on success or
 *  definitive failure and leaving retryable ones in place for the next pass.
 */
// VFALCO TODO rename to SortedTxSet
class CanonicalTXSet : public CountedObject<CanonicalTXSet>
{
private:
    /** Sort key for the internal transaction map.
     *
     *  Holds a salted account identifier, a `SeqProxy`, and the transaction
     *  hash. The three-level `operator<` groups transactions by account, then
     *  orders within an account by `SeqProxy` (sequences before tickets), then
     *  breaks ties by transaction ID.
     *
     *  `operator==` compares only `txId_` — identity is the transaction hash
     *  alone, independent of account or sequence context. This asymmetry is
     *  intentional: iterator-based `erase` must not conflate distinct
     *  transactions that happen to share account/sequence metadata.
     */
    class Key
    {
    public:
        Key(uint256 const& account, SeqProxy seqProx, uint256 const& id)
            : account_(account), txId_(id), seqProxy_(seqProx)
        {
        }

        friend bool
        operator<(Key const& lhs, Key const& rhs);

        friend bool
        operator>(Key const& lhs, Key const& rhs)
        {
            return rhs < lhs;
        }

        friend bool
        operator<=(Key const& lhs, Key const& rhs)
        {
            return !(lhs > rhs);
        }

        friend bool
        operator>=(Key const& lhs, Key const& rhs)
        {
            return !(lhs < rhs);
        }

        /** Tests equality by transaction ID only.
         *
         *  Deliberately asymmetric with `operator<`: two keys with different
         *  account/sequence values but the same `txId_` compare equal. This
         *  keeps iterator-based removal (`erase`) safe — the map's ordering
         *  key is account+seq+id, but uniqueness is solely the transaction
         *  hash.
         */
        friend bool
        operator==(Key const& lhs, Key const& rhs)
        {
            return lhs.txId_ == rhs.txId_;
        }

        friend bool
        operator!=(Key const& lhs, Key const& rhs)
        {
            return !(lhs == rhs);
        }

        /** Returns the salted account identifier used as the primary sort key. */
        [[nodiscard]] uint256 const&
        getAccount() const
        {
            return account_;
        }

        /** Returns the transaction hash. */
        [[nodiscard]] uint256 const&
        getTXID() const
        {
            return txId_;
        }

    private:
        uint256 account_;
        uint256 txId_;
        SeqProxy seqProxy_;
    };

    friend bool
    operator<(Key const& lhs, Key const& rhs);

    /** Computes the salted sort key for an account.
     *
     *  Copies the 20-byte `AccountID` into a zeroed `uint256`, then XORs the
     *  result with `salt_`. The XOR prevents an attacker from mining account
     *  addresses with low byte values to gain a persistent ordering advantage:
     *  because `salt_` changes each ledger round, the effective sort position
     *  of any account is randomized per round.
     */
    uint256
    accountKey(AccountID const& account);

public:
    using const_iterator = std::map<Key, std::shared_ptr<STTx const>>::const_iterator;

public:
    /** Constructs the set with the given ledger hash as the account-key salt.
     *
     *  @param saltHash Hash of the current ledger (or consensus map); used to
     *      randomize per-round account sort positions. Pass `uint256{}` when a
     *      stable, unsalted ordering is acceptable (e.g., `LocalTxs`).
     */
    explicit CanonicalTXSet(LedgerHash const& saltHash) : salt_(saltHash)
    {
    }

    /** Inserts a transaction into the set.
     *
     *  Constructs a `Key` from the transaction's salted account ID, `SeqProxy`,
     *  and transaction hash, then inserts the `(Key, tx)` pair into the map.
     *  Duplicate inserts (same transaction hash) are silently ignored by the
     *  underlying `std::map`.
     *
     *  @param txn The signed transaction to enqueue.
     */
    void
    insert(std::shared_ptr<STTx const> const& txn);

    /** Pops and returns the next eligible transaction for the same account.
     *
     *  After `tx` has been successfully applied to the open ledger, call this
     *  method to retrieve and remove the immediately-following transaction for
     *  the same account, if one exists and is eligible. A transaction is
     *  eligible if it either:
     *  - uses a ticket (tickets may be applied regardless of sequence gaps), or
     *  - has a sequence number exactly one greater than `tx`'s sequence.
     *
     *  The search uses `lower_bound` on a synthetic key whose `txId_` is
     *  `beast::zero` (which sorts before any real transaction ID) to locate the
     *  first map entry past `tx`'s position. If that entry belongs to a
     *  different account, or its sequence constraint is not satisfied, the
     *  method returns `nullptr`.
     *
     *  @param tx The just-applied transaction whose account and sequence
     *      establish the search anchor.
     *  @return The next eligible transaction (removed from the set), or
     *      `nullptr` if no suitable successor exists.
     */
    std::shared_ptr<STTx const>
    popAcctTransaction(std::shared_ptr<STTx const> const& tx);

    /** Resets the set for a new ledger round.
     *
     *  Installs a fresh salt and clears all transactions, allowing the same
     *  `CanonicalTXSet` instance to be reused across rounds without
     *  reallocating the underlying container.
     *
     *  @param salt New ledger hash to use as the account-key salt.
     */
    void
    reset(LedgerHash const& salt)
    {
        salt_ = salt;
        map_.clear();
    }

    /** Erases the transaction at `it` and returns an iterator to the next element.
     *
     *  Supports in-place removal during iteration, as used by `applyTransactions()`
     *  in `BuildLedger.cpp` when a transaction succeeds or definitively fails.
     *
     *  @param it A valid iterator into this set.
     *  @return Iterator to the element following the removed one.
     */
    const_iterator
    erase(const_iterator const& it)
    {
        return map_.erase(it);
    }

    /** Returns an iterator to the first transaction in canonical order. */
    [[nodiscard]] const_iterator
    begin() const
    {
        return map_.begin();
    }

    /** Returns a past-the-end iterator. */
    [[nodiscard]] const_iterator
    end() const
    {
        return map_.end();
    }

    /** Returns the number of transactions currently in the set. */
    [[nodiscard]] size_t
    size() const
    {
        return map_.size();
    }

    /** Returns `true` if the set contains no transactions. */
    [[nodiscard]] bool
    empty() const
    {
        return map_.empty();
    }

    /** Returns the salt hash that identifies this set's ordering context.
     *
     *  Callers use this for logging the transaction set identity alongside
     *  the ledger close time (e.g., `RCLConsensus` logs `retriableTxs.key()`
     *  when building the canonical set from the consensus map).
     */
    [[nodiscard]] uint256 const&
    key() const
    {
        return salt_;
    }

private:
    std::map<Key, std::shared_ptr<STTx const>> map_;

    // XORed into each account's sort key to prevent mining for low account
    // numbers that would gain a persistent ordering advantage. Refreshed each
    // ledger round via reset().
    uint256 salt_;
};

}  // namespace xrpl
