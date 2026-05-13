#pragma once

#include <xrpl/basics/CountedObject.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TxMeta.h>

#include <boost/container/flat_set.hpp>

namespace xrpl {

/** Immutable snapshot of a transaction accepted into a closed ledger.
 *
 *  Constructed once from the closed ledger view, the serialized transaction,
 *  and its raw metadata; all downstream representations — JSON payload, binary
 *  metadata blob, affected-account set — are fully materialized at construction
 *  time and never recomputed.
 *
 *  The pre-built `json_` payload is consumed directly by
 *  `NetworkOPsImp::pubValidatedTransaction()` and `pubAccountTransaction()`
 *  for WebSocket subscription delivery; `rawMeta_` (via `getEscMeta()`) feeds
 *  SQL `INSERT`/`REPLACE` statements in the relational transaction database.
 *
 *  `CountedObject` inheritance exposes live-instance telemetry useful for
 *  detecting accumulation under load or slow subscriber drain holding ledger
 *  snapshots open longer than expected.
 *
 *  @note Immutable after construction; safe to share across threads without
 *      additional locking.
 *  @note The ledger passed to the constructor must be closed (not open).
 *      Constructing from an open ledger aborts in debug builds.
 *  @see AcceptedLedger
 */
class AcceptedLedgerTx : public CountedObject<AcceptedLedgerTx>
{
public:
    /** Construct and fully materialize a closed-ledger transaction snapshot.
     *
     *  Parses metadata into a `TxMeta`, serializes raw metadata bytes, builds
     *  the complete JSON payload (transaction, meta, raw_meta, result, affected
     *  accounts), and — for non-self-funded `ttOFFER_CREATE` transactions —
     *  annotates the JSON with `owner_funds` queried from `accountFunds()` with
     *  freeze and auth checks bypassed. This avoids a later ledger round-trip
     *  when delivering to order-book subscribers.
     *
     *  @param ledger The closed ledger that accepted this transaction. Must not
     *      be open; the constructor asserts `!ledger->open()` in debug builds.
     *  @param txn The serialized transaction object.
     *  @param met The raw metadata `STObject` produced during transaction apply.
     */
    AcceptedLedgerTx(
        std::shared_ptr<ReadView const> const& ledger,
        std::shared_ptr<STTx const> const&,
        std::shared_ptr<STObject const> const&);

    /** Returns the serialized transaction. */
    [[nodiscard]] std::shared_ptr<STTx const> const&
    getTxn() const
    {
        return txn_;
    }

    /** Returns the parsed transaction metadata, including affected nodes and
     *  result code.
     */
    [[nodiscard]] TxMeta const&
    getMeta() const
    {
        return meta_;
    }

    /** Returns the set of accounts affected by this transaction.
     *
     *  Stored as a `flat_set` for cache-friendly iteration during subscription
     *  fan-out in `pubAccountTransaction()`.
     */
    [[nodiscard]] boost::container::flat_set<AccountID> const&
    getAffected() const
    {
        return affected_;
    }

    /** Returns the transaction's unique identifier (SHA-512 half of the
     *  canonical serialization).
     */
    [[nodiscard]] TxID
    getTransactionID() const
    {
        return txn_->getTransactionID();
    }

    /** Returns the transaction type (e.g., `ttOFFER_CREATE`, `ttPAYMENT`). */
    [[nodiscard]] TxType
    getTxnType() const
    {
        return txn_->getTxnType();
    }

    /** Returns the transaction result code as recorded in metadata. */
    [[nodiscard]] TER
    getResult() const
    {
        return meta_.getResultTER();
    }

    /** Returns the transaction's ordinal position within the closed ledger.
     *
     *  This is `TxMeta::getIndex()` — the transaction's sequence number within
     *  the ledger's ordered transaction set, not the account sequence number.
     */
    [[nodiscard]] std::uint32_t
    getTxnSeq() const
    {
        return meta_.getIndex();
    }

    /** Returns the raw metadata formatted as an escaped SQL blob literal.
     *
     *  Formats `rawMeta_` via `sqlBlobLiteral()` for direct embedding in SQL
     *  `INSERT`/`REPLACE` statements (see `STTx::getMetaSQL()` in `Node.cpp`).
     *
     *  @return SQL blob literal string suitable for verbatim inclusion in a
     *      SQL statement.
     *  @note Asserts that `rawMeta_` is non-empty. An empty blob indicates
     *      upstream ledger corruption; every accepted transaction must carry
     *      metadata.
     */
    [[nodiscard]] std::string
    getEscMeta() const;

    /** Returns the pre-built JSON envelope for WebSocket subscription delivery.
     *
     *  The object contains `transaction`, `meta`, `raw_meta` (hex), `result`
     *  (human-readable TER string), and `affected` (base58 account array).
     *  For non-self-funded `ttOFFER_CREATE` transactions, `transaction` also
     *  contains `owner_funds` — the account's spendable balance of the offered
     *  asset at acceptance time, computed with freeze and auth checks bypassed.
     */
    [[nodiscard]] json::Value const&
    getJson() const
    {
        return json_;
    }

private:
    std::shared_ptr<STTx const> txn_;
    TxMeta meta_;
    boost::container::flat_set<AccountID> affected_;
    Blob rawMeta_;
    json::Value json_;
};

}  // namespace xrpl
