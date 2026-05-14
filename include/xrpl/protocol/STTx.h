/** @file
 *  Declares `STTx`, the canonical in-memory representation of an XRP Ledger
 *  transaction, together with the free functions that operate on it
 *  (`passesLocalChecks`, `sterilize`, `isPseudoTx`).
 */
#pragma once

#include <xrpl/basics/Expected.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TxFormats.h>

#include <boost/container/flat_set.hpp>

#include <functional>

namespace xrpl {

/** Status codes used to tag a transaction row in the local SQLite
 *  `Transactions` table.
 *
 *  Each enumerator maps to the single-character `Status` column value
 *  stored by `getMetaSQL`.
 */
enum class TxnSql : char {
    New = 'N',       /**< Transaction has just been received and not yet processed. */
    Conflict = 'C',  /**< Transaction conflicts with a previously applied transaction. */
    Held = 'H',      /**< Transaction is queued but not yet eligible for inclusion. */
    Validated = 'V', /**< Transaction is in a validated ledger. */
    Included = 'I',  /**< Transaction is included in a pending ledger. */
    Unknown = 'U'    /**< Transaction status cannot be determined. */
};

/** The canonical in-memory representation of an XRP Ledger transaction.
 *
 *  `STTx` extends `STObject` with transaction-specific identity, typing,
 *  signing, and persistence semantics. It caches the transaction ID (`tid_`,
 *  a SHA-512 half-hash prefixed with `HashPrefix::transactionID`) and the
 *  decoded transaction type (`tx_type_`) so that hot paths avoid repeated
 *  field lookups and hash recomputation.
 *
 *  Three construction paths exist: wire deserialization (`SerialIter&`),
 *  object promotion (`STObject&&`), and programmatic assembly
 *  (`TxType, assembler`). Copy construction is allowed; copy assignment is
 *  deleted to prevent invariant violations on re-assignment.
 *
 *  The class is `final`: transaction-type-specific behavior lives in the
 *  transactor subsystem, not in subclasses of `STTx`.
 *
 *  @note `CountedObject<STTx>` tracks live instance counts for diagnostics.
 */
class STTx final : public STObject, public CountedObject<STTx>
{
    uint256 tid_;
    TxType tx_type_;

public:
    /** Minimum number of signers allowed in a multi-sign signer list. */
    static constexpr std::size_t kMIN_MULTI_SIGNERS = 1;
    /** Maximum number of signers allowed in a multi-sign signer list. */
    static constexpr std::size_t kMAX_MULTI_SIGNERS = 32;

    STTx() = delete;
    STTx(STTx const& other) = default;
    /** Deleted to prevent re-assignment from invalidating the cached ID and type. */
    STTx&
    operator=(STTx const& other) = delete;

    /** Deserialize a transaction from a wire-format byte stream.
     *
     *  Validates the remaining byte count against the
     *  `kTX_MIN_SIZE_BYTES`/`kTX_MAX_SIZE_BYTES` protocol bounds, parses
     *  the field stream, applies the `SOTemplate` for the decoded
     *  `TxType`, and caches the transaction ID. This is the hottest
     *  construction path: every inbound peer transaction and every
     *  transaction loaded from the node store passes through here.
     *
     *  @param sit A `SerialIter` positioned at the first byte of the
     *      serialized transaction. The iterator is advanced in place.
     *  @throws std::runtime_error if the byte count is outside protocol
     *      bounds, if an object-terminator byte is encountered at the top
     *      level, if the transaction type is unregistered, or if
     *      `applyTemplate` rejects the field layout.
     */
    explicit STTx(SerialIter& sit);

    /** Rvalue-reference overload that delegates to the lvalue constructor.
     *
     *  `SerialIter` is consumed by value semantics internally, so the
     *  rvalue is not actually moved; the `// NOLINT` in the inline
     *  definition acknowledges this.
     *
     *  @param sit A temporary `SerialIter`; forwarded to `STTx(SerialIter&)`.
     *  @throws std::runtime_error (same conditions as the lvalue overload).
     */
    explicit STTx(SerialIter&& sit);

    /** Promote a generic `STObject` to a fully typed transaction.
     *
     *  Used when a transaction arrives as a raw parsed object (e.g., from
     *  JSON deserialization) and must be graduated to a fully validated
     *  `STTx`. No wire-size checks are performed; `applyTemplate` enforces
     *  field conformance against the registered `SOTemplate` for the
     *  transaction type.
     *
     *  @param object An rvalue `STObject` that must already contain
     *      `sfTransactionType`. Consumed by the move.
     *  @throws std::runtime_error if the transaction type is unregistered
     *      or if `applyTemplate` rejects the field layout.
     */
    explicit STTx(STObject&& object);

    /** Programmatically construct a transaction of the given type.
     *
     *  Installs the `SOTemplate` for `type` and sets `sfTransactionType`,
     *  then invokes `assembler` to populate remaining fields. After the
     *  assembler returns, the transaction ID is computed and cached.
     *
     *  @param type      The transaction type; must be registered in
     *      `TxFormats`.
     *  @param assembler A callable invoked with a mutable reference to the
     *      newly templated `STObject`. Must not mutate `sfTransactionType`.
     *  @throws std::runtime_error if `type` is not registered.
     *  @note Fires `logicError` (not a thrown exception) if `assembler`
     *      mutates `sfTransactionType` — this is a programming error, not a
     *      data error.
     */
    STTx(TxType type, std::function<void(STObject&)> assembler);

    /** @return The serialized type ID `STI_TRANSACTION`. */
    SerializedTypeID
    getSType() const override;

    /** @return A human-readable string of the form `"<txid>" = { ... }`. */
    std::string
    getFullText() const override;

    /** Extract the raw `sfTxnSignature` bytes from an arbitrary object.
     *
     *  @param sigObject The object to read `sfTxnSignature` from; typically
     *      `*this` or a multi-sign signer sub-object.
     *  @return The signature bytes, or an empty `Blob` if the field is absent
     *      or an exception occurs during field access.
     */
    static Blob
    getSignature(STObject const& sigObject);

    /** Extract the `sfTxnSignature` bytes from this transaction. */
    Blob
    getSignature() const
    {
        return getSignature(*this);
    }

    /** Compute the single-sign hash of this transaction.
     *
     *  Prepends `HashPrefix::TxSign` to the serialized form (without signing
     *  fields) and returns the SHA-512 half-hash. Use this to verify a
     *  signature without calling `checkSign`.
     *
     *  @return The 256-bit signing hash.
     */
    uint256
    getSigningHash() const;

    /** @return The decoded transaction type, cached at construction time. */
    TxType
    getTxnType() const;

    /** @return The raw bytes of `sfSigningPubKey`. Empty for multi-signed transactions. */
    Blob
    getSigningPubKey() const;

    /** Return a unified sequence proxy abstracting classic sequence and ticket modes.
     *
     *  When `sfSequence` is non-zero the transaction uses classic sequence
     *  ordering and a `SeqProxy::Sequence` is returned. When `sfSequence` is
     *  zero and `sfTicketSequence` is present, a `SeqProxy::Ticket` is returned.
     *  Sequence-type proxies always sort before ticket-type proxies, which the
     *  protocol relies on for correct processing order.
     *
     *  @return A `SeqProxy` of type `Sequence` or `Ticket` as appropriate.
     */
    SeqProxy
    getSeqProxy() const;

    /** Returns the first non-zero value of (Sequence, TicketSequence). */
    std::uint32_t
    getSeqValue() const;

    /** Resolve the account whose balance pays the transaction fee.
     *
     *  Returns `sfDelegate` if present, otherwise `sfAccount`. Authorization
     *  of the delegate relationship is enforced separately in the transactor
     *  layer; this method performs no validation.
     *
     *  @return The `AccountID` of the fee-paying account.
     */
    AccountID
    getFeePayer() const;

    /** Collect every `AccountID` referenced by top-level fields of this transaction.
     *
     *  Walks top-level `STAccount` fields and non-XRP `STAmount` issuers.
     *  Used to determine which accounts are touched by a transaction for
     *  indexing and fee purposes.
     *
     *  @return A flat, sorted set of all referenced account IDs.
     *  @note Only top-level fields are examined; nested objects (e.g.,
     *      inner multi-sign signers) are not descended into.
     */
    boost::container::flat_set<AccountID>
    getMentionedAccounts() const;

    /** @return The cached transaction ID, computed at construction time. */
    uint256
    getTransactionID() const;

    /** Return the transaction as a JSON object, optionally including the hash.
     *
     *  Includes the `"hash"` key unless `options` has
     *  `JsonOptions::DisableApiPriorV2` set (API v2+).
     *
     *  @param options JSON rendering options.
     *  @return A `json::Value` object representing the transaction.
     */
    json::Value
    getJson(JsonOptions options) const override;

    /** Return the transaction as JSON, with an optional binary representation.
     *
     *  When `binary` is `true`, the transaction body is hex-encoded. Under
     *  API v1 the result wraps the hex in `{"tx": "...", "hash": "..."}`;
     *  under API v2+ it returns the raw hex string. When `binary` is `false`,
     *  behaves identically to `getJson(options)`.
     *
     *  @param options JSON rendering options controlling API version behavior.
     *  @param binary  If `true`, serialize the transaction to hex instead of
     *      expanding fields into JSON.
     *  @return A `json::Value` containing the transaction representation.
     */
    json::Value
    getJson(JsonOptions options, bool binary) const;

    /** Sign this transaction with the given key pair.
     *
     *  Computes the single-sign payload (hash-prefix + transaction body
     *  without signing fields), signs it, and writes the signature and public
     *  key into the transaction. The cached transaction ID is recomputed
     *  after the signature is stored.
     *
     *  @param publicKey       The signer's public key; written to
     *      `sfSigningPubKey`.
     *  @param secretKey       The corresponding secret key used to produce
     *      the signature.
     *  @param signatureTarget If set, the signature is written into that
     *      named sub-object field (e.g., `sfCounterpartySignature`) instead
     *      of the transaction root. Used for two-party protocols such as
     *      `LoanSet`.
     */
    void
    sign(
        PublicKey const& publicKey,
        SecretKey const& secretKey,
        std::optional<std::reference_wrapper<SField const>> signatureTarget = {});

    /** Verify the primary signature and, if present, the counterparty signature.
     *
     *  Dispatches to single-sign or multi-sign verification based on whether
     *  `sfSigningPubKey` is empty. If `sfCounterpartySignature` is present,
     *  it is verified with the same dispatch; errors from the counterparty
     *  check are prefixed with `"Counterparty: "`.
     *
     *  @param rules The current ledger rules.
     *  @return An empty `Expected` on success, or an error string on failure.
     */
    Expected<void, std::string>
    checkSign(Rules const& rules) const;

    /** Verify all batch-signing signatures on a `ttBATCH` transaction.
     *
     *  Iterates over `sfBatchSigners`, dispatching each entry to single- or
     *  multi-sign batch verification. The signed payload is the output of
     *  `serializeBatch()` — a batch-specific hash prefix, the outer
     *  transaction's flags, and the IDs of the inner transactions — which
     *  binds each signer to the exact set of inner transactions.
     *
     *  @param rules The current ledger rules.
     *  @return An empty `Expected` on success, or an error string on failure.
     *  @note Asserts and returns an error if called on a non-batch transaction.
     */
    Expected<void, std::string>
    checkBatchSign(Rules const& rules) const;

    /** Return the static SQL `INSERT OR REPLACE INTO Transactions` header.
     *
     *  The returned string is the constant prefix used by `getMetaSQL` to
     *  build persistence statements for the local SQLite `Transactions` table.
     *
     *  @return A reference to a process-lifetime static string.
     */
    static std::string const&
    getMetaSQLInsertReplaceHeader();

    /** Produce a SQL value tuple for this transaction with `Validated` status.
     *
     *  Serializes the transaction and delegates to the full overload with
     *  `TxnSql::Validated` as the status code.
     *
     *  @param inLedger        The ledger sequence number containing this
     *      transaction.
     *  @param escapedMetaData Pre-escaped binary metadata string for the
     *      `TxnMeta` column.
     *  @return A SQL value tuple string suitable for appending to
     *      `getMetaSQLInsertReplaceHeader()`.
     */
    std::string
    getMetaSQL(std::uint32_t inLedger, std::string const& escapedMetaData) const;

    /** Produce a SQL value tuple with explicit status and raw transaction bytes.
     *
     *  Formats a parenthesized row for the `Transactions` table containing
     *  the transaction ID, type name, source account (Base58), sequence
     *  number, ledger sequence, a single-character status code, the raw
     *  serialized transaction blob, and pre-escaped metadata.
     *
     *  @param rawTxn          The serialized transaction bytes (by value).
     *  @param inLedger        The ledger sequence number containing this
     *      transaction.
     *  @param status          The persistence status code for the `Status`
     *      column.
     *  @param escapedMetaData Pre-escaped binary metadata for `TxnMeta`.
     *  @return A SQL value tuple string.
     */
    std::string
    getMetaSQL(
        Serializer rawTxn,
        std::uint32_t inLedger,
        TxnSql status,
        std::string const& escapedMetaData) const;

    /** Return the cached IDs of the inner transactions in a `ttBATCH` transaction.
     *
     *  On the first call, hashes each entry in `sfRawTransactions` and
     *  stores the result in `batchTxnIds_`. Subsequent calls return the
     *  cached vector directly. An assertion on every call verifies that the
     *  cache size still matches `sfRawTransactions`, enforcing the invariant
     *  that inner transactions may not be modified after the IDs have been
     *  observed.
     *
     *  @return A const reference to the cached vector of inner transaction IDs.
     *  @note Must only be called on a `ttBATCH` transaction with a non-empty
     *      `sfRawTransactions` array.
     */
    std::vector<uint256> const&
    getBatchTransactionIDs() const;

private:
    /** Dispatch to single- or multi-sign verification for an arbitrary object.
     *
     *  Inspects `sfSigningPubKey` in `sigObject`: empty → multi-sign path,
     *  non-empty → single-sign path.
     *
     *  @param rules     The current ledger rules.
     *  @param sigObject The object carrying signature fields; usually `*this`
     *      but may be a counterparty sub-object.
     *  @return An empty `Expected` on success, or an error string on failure.
     */
    Expected<void, std::string>
    checkSign(Rules const& rules, STObject const& sigObject) const;

    /** Verify a single-sign signature against the transaction body. */
    Expected<void, std::string>
    checkSingleSign(STObject const& sigObject) const;

    /** Verify multi-sign signatures against the transaction body. */
    Expected<void, std::string>
    checkMultiSign(Rules const& rules, STObject const& sigObject) const;

    /** Verify a single-sign batch signature for one `sfBatchSigners` entry. */
    Expected<void, std::string>
    checkBatchSingleSign(STObject const& batchSigner) const;

    /** Verify multi-sign batch signatures for one `sfBatchSigners` entry. */
    Expected<void, std::string>
    checkBatchMultiSign(STObject const& batchSigner, Rules const& rules) const;

    /** Placement-new copy into a pre-allocated buffer; supports `STVar` SOO. */
    STBase*
    copy(std::size_t n, void* buf) const override;
    /** Placement-new move into a pre-allocated buffer; supports `STVar` SOO. */
    STBase*
    move(std::size_t n, void* buf) override;

    friend class detail::STVar;
    /** Lazily populated cache of inner transaction IDs for `ttBATCH` transactions. */
    mutable std::vector<uint256> batchTxnIds_;
};

/** Run all local pre-submission validity checks on a transaction object.
 *
 *  Gate-keeps local relay and submission by enforcing:
 *  - Memo field size (max 1024 bytes serialized) and RFC 3986 character
 *    legality for `MemoType`/`MemoFormat`.
 *  - All `STAccount` fields must carry non-zero (non-default) values.
 *  - Pseudo-transaction types (`ttAMENDMENT`, `ttFEE`, `ttUNL_MODIFY`) are
 *    rejected; they are synthesized internally by the ledger.
 *  - MPT amounts may only appear in fields that explicitly declare MPT support
 *    via `soeMPTSupported`.
 *  - Batch inner transactions must not themselves be `ttBATCH`, and the
 *    `sfRawTransactions` / `sfBatchSigners` arrays must not exceed
 *    `kMAX_BATCH_TX_COUNT` entries.
 *
 *  This is a free function rather than an `STTx` method because it can run
 *  on any `STObject` before it is promoted to a full `STTx`.
 *
 *  @param st     The transaction object to validate.
 *  @param reason Populated with a human-readable failure description when
 *      the function returns `false`.
 *  @return `true` if all checks pass; `false` on the first failure.
 */
bool
passesLocalChecks(STObject const& st, std::string& reason);

/** Canonicalize a transaction via a serialize-then-deserialize round trip.
 *
 *  Serializes `stx` to bytes, then constructs a fresh `STTx` from those bytes
 *  via `SerialIter`. The result is in wire-canonical form: all equivalent
 *  in-memory representations collapse to the same byte sequence, field
 *  ordering is normalized, and the transaction ID is freshly computed.
 *
 *  Any code that synthesizes a transaction from JSON or via the programmatic
 *  assembler constructor and then submits it to the consensus pipeline should
 *  call `sterilize` first.
 *
 *  @param stx The source transaction to sterilize.
 *  @return A `shared_ptr` to the newly constructed canonical `STTx const`.
 *  @throws std::runtime_error if the round-trip deserialization fails.
 */
std::shared_ptr<STTx const>
sterilize(STTx const& stx);

/** Determine whether a transaction object is a ledger-generated pseudo-transaction.
 *
 *  Pseudo-transactions (`ttAMENDMENT`, `ttFEE`, `ttUNL_MODIFY`) are
 *  synthesized internally by the ledger and must never be submitted by
 *  external clients. `passesLocalChecks` rejects any object for which this
 *  returns `true`.
 *
 *  @param tx The transaction object to test; need not be a fully constructed
 *      `STTx`.
 *  @return `true` if the object carries a pseudo-transaction type.
 */
bool
isPseudoTx(STObject const& tx);

inline STTx::STTx(SerialIter&& sit)  // NOLINT(cppcoreguidelines-rvalue-reference-param-not-moved)
    : STTx(sit)
{
}

inline TxType
STTx::getTxnType() const
{
    return tx_type_;
}

inline Blob
STTx::getSigningPubKey() const
{
    return getFieldVL(sfSigningPubKey);
}

inline uint256
STTx::getTransactionID() const
{
    return tid_;
}

}  // namespace xrpl
