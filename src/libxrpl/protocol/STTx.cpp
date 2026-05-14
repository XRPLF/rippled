/** @file
 *  Implements `STTx` — the canonical in-memory representation of an XRP
 *  Ledger transaction.
 *
 *  Contains the three construction paths (wire deserialization, object
 *  promotion, and programmatic assembly), all four signing modes (single,
 *  multi, batch single, batch multi), counterparty signature support,
 *  local pre-submission validation (`passesLocalChecks`), and SQL
 *  persistence helpers for the `Transactions` database table.
 */
#include <xrpl/protocol/STTx.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/Expected.h>
#include <xrpl/basics/Log.h>
#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/SOTemplate.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/jss.h>

#include <boost/container/flat_set.hpp>
#include <boost/format/free_funcs.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace xrpl {

/** Look up the registered format for a transaction type.
 *
 *  @param type The transaction type to look up.
 *  @return A pointer to the `KnownFormat` entry for `type`.
 *  @throws std::runtime_error if `type` is not registered in `TxFormats`.
 */
static auto
getTxFormat(TxType type)
{
    auto format = TxFormats::getInstance().findByType(type);

    if (format == nullptr)
    {
        Throw<std::runtime_error>(
            "Invalid transaction type " +
            std::to_string(safeCast<std::underlying_type_t<TxType>>(type)));
    }

    return format;
}

/** Promote an already-parsed `STObject` into a fully validated `STTx`.
 *
 *  Used when a transaction has been reconstructed from JSON or another
 *  in-memory representation. No wire-size checks are performed because
 *  the object is already in memory; `applyTemplate` enforces field
 *  conformance against the registered `SOTemplate` for the transaction
 *  type. The transaction ID is computed and cached on exit.
 *
 *  @param object An rvalue `STObject` that must already contain
 *      `sfTransactionType`. The object is consumed by the move.
 *  @throws std::runtime_error if the transaction type is not registered
 *      or if `applyTemplate` rejects the field layout.
 */
STTx::STTx(STObject&& object) : STObject(std::move(object))
{
    tx_type_ = safeCast<TxType>(getFieldU16(sfTransactionType));
    applyTemplate(getTxFormat(tx_type_)->getSOTemplate());  //  may throw
    tid_ = getHash(HashPrefix::TransactionId);
}

/** Deserialize a transaction from a wire-format byte stream.
 *
 *  This is the hottest construction path: every inbound transaction and
 *  every transaction loaded from disk passes through it. Size bounds are
 *  checked before field parsing to reject pathological input early.
 *  After parsing, an object-terminator byte found at the top level
 *  (`set()` returning `true`) indicates a structurally invalid stream.
 *
 *  @param sit A `SerialIter` positioned at the first byte of the
 *      serialized transaction. The iterator is advanced by the call.
 *  @throws std::runtime_error if the byte count is outside
 *      [`kTX_MIN_SIZE_BYTES`, `kTX_MAX_SIZE_BYTES`], if an object
 *      terminator is encountered at the top level, if the transaction
 *      type is unregistered, or if field layout fails `applyTemplate`.
 */
STTx::STTx(SerialIter& sit) : STObject(sfTransaction)
{
    int const length = sit.getBytesLeft();

    if ((length < kTX_MIN_SIZE_BYTES) || (length > kTX_MAX_SIZE_BYTES))
        Throw<std::runtime_error>("Transaction length invalid");

    if (set(sit))
        Throw<std::runtime_error>("Transaction contains an object terminator");

    tx_type_ = safeCast<TxType>(getFieldU16(sfTransactionType));

    applyTemplate(getTxFormat(tx_type_)->getSOTemplate());  // May throw
    tid_ = getHash(HashPrefix::TransactionId);
}

/** Programmatically construct a transaction of the given type.
 *
 *  Installs the `SOTemplate` for `type` first so the object has the
 *  correct field scaffolding, then invokes `assembler` to populate
 *  fields. After the assembler returns, `sfTransactionType` is read
 *  back; if the assembler mutated it, `logicError` fires rather than
 *  `std::runtime_error` because that is a programming mistake, not a
 *  data error.
 *
 *  @param type     The transaction type, which must be registered in
 *      `TxFormats`.
 *  @param assembler A callable invoked with a mutable reference to the
 *      newly constructed `STObject`. It must not change
 *      `sfTransactionType`.
 *  @throws std::runtime_error if `type` is not registered.
 *  @note Fires `logicError` (not an exception) if `assembler` mutates
 *      `sfTransactionType`.
 */
STTx::STTx(TxType type, std::function<void(STObject&)> assembler) : STObject(sfTransaction)
{
    auto format = getTxFormat(type);

    set(format->getSOTemplate());
    setFieldU16(sfTransactionType, format->getType());

    assembler(*this);

    tx_type_ = safeCast<TxType>(getFieldU16(sfTransactionType));

    if (tx_type_ != type)
        logicError("Transaction type was mutated during assembly");

    tid_ = getHash(HashPrefix::TransactionId);
}

STBase*
STTx::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STTx::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

// STObject functions.
SerializedTypeID
STTx::getSType() const
{
    return STI_TRANSACTION;
}

std::string
STTx::getFullText() const
{
    std::string ret = "\"";
    ret += to_string(getTransactionID());
    ret += "\" = {";
    ret += STObject::getFullText();
    ret += "}";
    return ret;
}

/** Collect every `AccountID` referenced by this transaction.
 *
 *  Walks the top-level fields of the transaction and adds each
 *  non-default `STAccount` value and each non-XRP `STAmount` issuer
 *  to the result set. Used to determine which accounts are touched by
 *  a transaction for indexing and fee purposes.
 *
 *  @return A flat, sorted set of all referenced account IDs.
 *  @note Only top-level fields are examined; nested objects (e.g.,
 *      inner multi-sign signers) are not descended into.
 */
boost::container::flat_set<AccountID>
STTx::getMentionedAccounts() const
{
    boost::container::flat_set<AccountID> list;

    for (auto const& it : *this)
    {
        if (auto sacc = dynamic_cast<STAccount const*>(&it))
        {
            XRPL_ASSERT(!sacc->isDefault(), "xrpl::STTx::getMentionedAccounts : account is set");
            if (!sacc->isDefault())
                list.insert(sacc->value());
        }
        else if (auto samt = dynamic_cast<STAmount const*>(&it))
        {
            auto const& issuer = samt->getIssuer();
            if (!isXRP(issuer))
                list.insert(issuer);
        }
    }

    return list;
}

/** Produce the canonical single-sign payload for a transaction.
 *
 *  Prepends `HashPrefix::TxSign` to the transaction serialized without
 *  its signature fields (`addWithoutSigningFields`). This is the exact
 *  byte sequence that is signed and verified for single-signed
 *  transactions.
 *
 *  @param that The transaction to serialize.
 *  @return The signing payload as a byte blob.
 */
static Blob
getSigningData(STTx const& that)
{
    Serializer s;
    s.add32(HashPrefix::TxSign);
    that.addWithoutSigningFields(s);
    return s.getData();
}

uint256
STTx::getSigningHash() const
{
    return STObject::getSigningHash(HashPrefix::TxSign);
}

/** Extract the raw `sfTxnSignature` bytes from an object.
 *
 *  @param sigObject The object to read the signature field from;
 *      typically `*this` or a multi-sign signer sub-object.
 *  @return The signature bytes, or an empty `Blob` if the field is
 *      absent or an exception is thrown during access.
 */
Blob
STTx::getSignature(STObject const& sigObject)
{
    try
    {
        return sigObject.getFieldVL(sfTxnSignature);
    }
    catch (std::exception const&)
    {
        return Blob();
    }
}

/** Return a unified sequence proxy for this transaction.
 *
 *  When `sfSequence` is non-zero the transaction uses classic sequence
 *  ordering. When `sfSequence` is zero and `sfTicketSequence` is
 *  present, the transaction consumes a ticket. In either case the
 *  returned `SeqProxy` comparison operators guarantee that
 *  sequence-type values sort before ticket-type values, preserving the
 *  correct processing order between ticket-creating and
 *  ticket-consuming transactions.
 *
 *  @return A `SeqProxy` of type `Sequence` or `Ticket` as appropriate.
 */
SeqProxy
STTx::getSeqProxy() const
{
    std::uint32_t const seq{getFieldU32(sfSequence)};
    if (seq != 0)
        return SeqProxy::sequence(seq);

    std::optional<std::uint32_t> const ticketSeq{operator[](~sfTicketSequence)};
    if (!ticketSeq)
    {
        // No TicketSequence specified.  Return the Sequence, whatever it is.
        return SeqProxy::sequence(seq);
    }

    return SeqProxy{SeqProxy::Type::Ticket, *ticketSeq};
}

std::uint32_t
STTx::getSeqValue() const
{
    return getSeqProxy().value();
}

/** Resolve the account whose balance pays the transaction fee.
 *
 *  When `sfDelegate` is present the delegate account bears the fee;
 *  otherwise the transaction's `sfAccount` pays. This method performs
 *  no authorization — the delegate's right to act on behalf of the
 *  account owner is enforced in `Transactor::checkPermission`, and the
 *  cryptographic validity of the delegate's signature is verified in
 *  `Transactor::checkSign`.
 *
 *  @return The `AccountID` of the fee-paying account.
 */
AccountID
STTx::getFeePayer() const
{
    if (isFieldPresent(sfDelegate))
        return getAccountID(sfDelegate);

    return getAccountID(sfAccount);
}

/** Sign this transaction with the given key pair.
 *
 *  Computes the single-sign payload via `getSigningData`, signs it,
 *  and writes the resulting signature into `sfTxnSignature`. If
 *  `signatureTarget` is supplied the signature is written into that
 *  field's nested object instead of the top-level transaction — used
 *  for counterparty signing (e.g., `LoanSet`). The cached transaction
 *  ID is recomputed after the signature is stored because the content
 *  has changed.
 *
 *  @param publicKey       The signer's public key, written to
 *      `sfSigningPubKey`.
 *  @param secretKey       The corresponding secret key used to produce
 *      the signature.
 *  @param signatureTarget If set, names the sub-object field (e.g.,
 *      `sfCounterpartySignature`) into which the signature is written
 *      instead of the transaction root.
 */
void
STTx::sign(
    PublicKey const& publicKey,
    SecretKey const& secretKey,
    std::optional<std::reference_wrapper<SField const>> signatureTarget)
{
    auto const data = getSigningData(*this);

    auto const sig = xrpl::sign(publicKey, secretKey, makeSlice(data));

    if (signatureTarget)
    {
        auto& target = peekFieldObject(*signatureTarget);
        target.setFieldVL(sfTxnSignature, sig);
    }
    else
    {
        setFieldVL(sfTxnSignature, sig);
    }
    tid_ = getHash(HashPrefix::TransactionId);
}

/** Dispatch a signature check to the single- or multi-sign verifier.
 *
 *  Inspects `sfSigningPubKey` in `sigObject` to determine the signing
 *  mode: an empty key indicates multi-sign, a non-empty key indicates
 *  single-sign.
 *
 *  @param rules     The current ledger rules.
 *  @param sigObject The object that carries the signature fields;
 *      usually `*this` but may be a counterparty sub-object.
 *  @return An empty `Expected` on success, or an `Expected` holding
 *      an error string describing the failure.
 */
Expected<void, std::string>
STTx::checkSign(Rules const& rules, STObject const& sigObject) const
{
    try
    {
        // Determine whether we're single- or multi-signing by looking
        // at the SigningPubKey.  If it's empty we must be
        // multi-signing.  Otherwise we're single-signing.

        Blob const& signingPubKey = sigObject.getFieldVL(sfSigningPubKey);
        return signingPubKey.empty() ? checkMultiSign(rules, sigObject)
                                     : checkSingleSign(sigObject);
    }
    catch (...)
    {
        return Unexpected("Internal signature check failure.");
    }
}

/** Verify the primary signature and, if present, the counterparty signature.
 *
 *  Checks the primary (single- or multi-) signature on the transaction.
 *  If `sfCounterpartySignature` is present, its signature is verified
 *  with the same dispatch; errors from the counterparty check are
 *  prefixed with `"Counterparty: "` so callers can distinguish which
 *  party failed.
 *
 *  @param rules The current ledger rules.
 *  @return An empty `Expected` on success, or an `Expected` holding an
 *      error description.
 */
Expected<void, std::string>
STTx::checkSign(Rules const& rules) const
{
    if (auto const ret = checkSign(rules, *this); !ret)
        return ret;

    if (isFieldPresent(sfCounterpartySignature))
    {
        auto const counterSig = getFieldObject(sfCounterpartySignature);
        if (auto const ret = checkSign(rules, counterSig); !ret)
            return Unexpected("Counterparty: " + ret.error());
    }
    return {};
}

/** Verify all batch-signing signatures on a `ttBATCH` transaction.
 *
 *  Iterates over `sfBatchSigners`, dispatching each entry to
 *  `checkBatchSingleSign` or `checkBatchMultiSign` based on whether
 *  `sfSigningPubKey` is empty. The signed payload for batch signers is
 *  the output of `serializeBatch()` — a batch-specific hash prefix,
 *  the outer transaction flags, and the IDs of the inner transactions
 *  — rather than the standard transaction signing payload.
 *
 *  @param rules The current ledger rules.
 *  @return An empty `Expected` on success, or an `Expected` holding an
 *      error description.
 *  @note Asserts (and returns an error) if called on a non-batch
 *      transaction.
 */
Expected<void, std::string>
STTx::checkBatchSign(Rules const& rules) const
{
    try
    {
        XRPL_ASSERT(getTxnType() == ttBATCH, "STTx::checkBatchSign : not a batch transaction");
        if (getTxnType() != ttBATCH)
        {
            JLOG(debugLog().fatal()) << "not a batch transaction";
            return Unexpected("Not a batch transaction.");
        }
        STArray const& signers{getFieldArray(sfBatchSigners)};
        for (auto const& signer : signers)
        {
            Blob const& signingPubKey = signer.getFieldVL(sfSigningPubKey);
            auto const result = signingPubKey.empty() ? checkBatchMultiSign(signer, rules)
                                                      : checkBatchSingleSign(signer);

            if (!result)
                return result;
        }
        return {};
    }
    catch (std::exception const& e)
    {
        JLOG(debugLog().error()) << "Batch signature check failed: " << e.what();
    }
    return Unexpected("Internal batch signature check failure.");
}

json::Value
STTx::getJson(JsonOptions options) const
{
    json::Value ret = STObject::getJson(JsonOptions::Values::None);
    if (!(options & JsonOptions::Values::DisableApiPriorV2))
        ret[jss::hash] = to_string(getTransactionID());
    return ret;
}

json::Value
STTx::getJson(JsonOptions options, bool binary) const
{
    bool const v1 = !(options & JsonOptions::Values::DisableApiPriorV2);

    if (binary)
    {
        Serializer const s = STObject::getSerializer();
        std::string const dataBin = strHex(s.peekData());

        if (v1)
        {
            json::Value ret(json::ValueType::Object);
            ret[jss::tx] = dataBin;
            ret[jss::hash] = to_string(getTransactionID());
            return ret;
        }

        return json::Value{dataBin};
    }

    json::Value ret = STObject::getJson(JsonOptions::Values::None);
    if (v1)
        ret[jss::hash] = to_string(getTransactionID());

    return ret;
}

/** Return the static SQL header for inserting a transaction row.
 *
 *  The returned string is the `INSERT OR REPLACE INTO Transactions`
 *  prefix used by `getMetaSQL`. Callers append one or more
 *  parenthesized value tuples produced by `getMetaSQL`.
 *
 *  @return A reference to a process-lifetime static string containing
 *      the SQL header.
 */
std::string const&
STTx::getMetaSQLInsertReplaceHeader()
{
    static std::string const kSQL =
        "INSERT OR REPLACE INTO Transactions "
        "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, "
        "TxnMeta)"
        " VALUES ";

    return kSQL;
}

/** Produce a SQL value tuple for this validated transaction.
 *
 *  Serializes the transaction, then delegates to the full overload with
 *  `TxnSql::Validated` status.
 *
 *  @param inLedger        The ledger sequence number that contains this
 *      transaction.
 *  @param escapedMetaData Pre-escaped binary metadata string for the
 *      `TxnMeta` column.
 *  @return A SQL value tuple string suitable for appending to
 *      `getMetaSQLInsertReplaceHeader()`.
 */
std::string
STTx::getMetaSQL(std::uint32_t inLedger, std::string const& escapedMetaData) const
{
    Serializer s;
    add(s);
    return getMetaSQL(s, inLedger, TxnSql::Validated, escapedMetaData);
}

// VFALCO This could be a free function elsewhere
/** Produce a SQL value tuple with explicit status and raw transaction bytes.
 *
 *  Formats a parenthesized row for the `Transactions` table containing
 *  the transaction ID, type name, source account (Base58), sequence
 *  number, ledger sequence, a single-character status code, the raw
 *  serialized transaction blob, and pre-escaped metadata.
 *
 *  @param rawTxn          The serialized transaction bytes (by value;
 *      the caller may pass the result of `STObject::getSerializer()`).
 *  @param inLedger        The ledger sequence number containing this
 *      transaction.
 *  @param status          The persistence status code for the
 *      `Status` column.
 *  @param escapedMetaData Pre-escaped binary metadata for `TxnMeta`.
 *  @return A SQL value tuple string.
 */
std::string
STTx::getMetaSQL(
    Serializer rawTxn,
    std::uint32_t inLedger,
    TxnSql status,
    std::string const& escapedMetaData) const
{
    static boost::format const kBF_TRANS("('%s', '%s', '%s', '%d', '%d', '%c', %s, %s)");
    std::string rTxn = sqlBlobLiteral(rawTxn.peekData());

    auto format = TxFormats::getInstance().findByType(tx_type_);
    XRPL_ASSERT(format, "xrpl::STTx::getMetaSQL : non-null type format");

    return str(
        boost::format(kBF_TRANS) % to_string(getTransactionID()) % format->getName() %
        toBase58(getAccountID(sfAccount)) % getFieldU32(sfSequence) % inLedger %
        safeCast<char>(status) % rTxn % escapedMetaData);
}

/** Verify a single signature against a pre-built signing payload.
 *
 *  Rejects the signature immediately if `sfSigners` is also present,
 *  which would mean the object is signed two ways simultaneously.
 *  Validates the public key type before attempting `verify()`;
 *  any exception during field access or verification is treated as an
 *  invalid signature.
 *
 *  @param sigObject The object carrying `sfSigningPubKey` and
 *      `sfTxnSignature`.
 *  @param data      The exact bytes that were signed (hash-prefixed
 *      payload).
 *  @return An empty `Expected` on success, or an error string.
 */
static Expected<void, std::string>
singleSignHelper(STObject const& sigObject, Slice const& data)
{
    // We don't allow both a non-empty sfSigningPubKey and an sfSigners.
    // That would allow the transaction to be signed two ways.  So if both
    // fields are present the signature is invalid.
    if (sigObject.isFieldPresent(sfSigners))
        return Unexpected("Cannot both single- and multi-sign.");

    bool validSig = false;
    try
    {
        auto const spk = sigObject.getFieldVL(sfSigningPubKey);
        if (publicKeyType(makeSlice(spk)))
        {
            Blob const signature = sigObject.getFieldVL(sfTxnSignature);
            validSig = verify(PublicKey(makeSlice(spk)), data, makeSlice(signature));
        }
    }
    catch (std::exception const&)
    {
        validSig = false;
    }

    if (!validSig)
        return Unexpected("Invalid signature.");

    return {};
}

/** Verify a single-sign signature on this transaction.
 *
 *  Builds the signing payload from the transaction content and
 *  delegates to `singleSignHelper`.
 *
 *  @param sigObject The object containing the signature fields. Usually
 *      `*this`, but may be a counterparty sub-object.
 *  @return An empty `Expected` on success, or an error string.
 */
Expected<void, std::string>
STTx::checkSingleSign(STObject const& sigObject) const
{
    auto const data = getSigningData(*this);
    return singleSignHelper(sigObject, makeSlice(data));
}

/** Verify a single-sign batch signature for one entry in `sfBatchSigners`.
 *
 *  The signing payload is the batch-specific serialization produced by
 *  `serializeBatch` (hash prefix, outer flags, inner transaction IDs)
 *  rather than the standard transaction payload. This ties the
 *  signature to a specific set of inner transactions.
 *
 *  @param batchSigner The signer sub-object from `sfBatchSigners`.
 *  @return An empty `Expected` on success, or an error string.
 */
Expected<void, std::string>
STTx::checkBatchSingleSign(STObject const& batchSigner) const
{
    Serializer msg;
    serializeBatch(msg, getFlags(), getBatchTransactionIDs());
    return singleSignHelper(batchSigner, msg.slice());
}

/** Core multi-sign verification loop shared by regular and batch paths.
 *
 *  Enforces the multi-sign invariants:
 *  - `sfSigners` must be present and `sfTxnSignature` must be absent
 *    (prevents dual-signing).
 *  - Signer count must be in [`kMIN_MULTI_SIGNERS`, `kMAX_MULTI_SIGNERS`].
 *  - Signers must appear in strictly ascending `AccountID` order (no
 *    duplicates).
 *  - The transaction owner (`txnAccountID`) may not appear as one of
 *    their own multi-signers; pass `std::nullopt` to skip this check
 *    (used for batch signing where the owner constraint differs).
 *  - Each signer's verification message is built per-signer via
 *    `makeMsg(accountID)`, which appends the signer's `AccountID` to
 *    a shared prefix — preventing cross-account signature replay.
 *
 *  @param sigObject     The object containing `sfSigners`.
 *  @param txnAccountID  The account that submitted the transaction, or
 *      `std::nullopt` to bypass the self-multisign check.
 *  @param makeMsg       Callable that constructs the per-signer signing
 *      payload given the signer's `AccountID`.
 *  @param rules         The current ledger rules.
 *  @return An empty `Expected` on success, or an error string
 *      identifying the failing signer.
 */
Expected<void, std::string>
multiSignHelper(
    STObject const& sigObject,
    std::optional<AccountID> txnAccountID,
    std::function<Serializer(AccountID const&)> makeMsg,
    Rules const& rules)
{
    // Make sure the MultiSigners are present.  Otherwise they are not
    // attempting multi-signing and we just have a bad SigningPubKey.
    if (!sigObject.isFieldPresent(sfSigners))
        return Unexpected("Empty SigningPubKey.");

    // We don't allow both an sfSigners and an sfTxnSignature.  Both fields
    // being present would indicate that the transaction is signed both ways.
    if (sigObject.isFieldPresent(sfTxnSignature))
        return Unexpected("Cannot both single- and multi-sign.");

    STArray const& signers{sigObject.getFieldArray(sfSigners)};

    // There are well known bounds that the number of signers must be within.
    if (signers.size() < STTx::kMIN_MULTI_SIGNERS || signers.size() > STTx::kMAX_MULTI_SIGNERS)
        return Unexpected("Invalid Signers array size.");

    // Signers must be in sorted order by AccountID.
    AccountID lastAccountID(beast::kZERO);

    for (auto const& signer : signers)
    {
        auto const accountID = signer.getAccountID(sfAccount);

        // The account owner may not usually multisign for themselves.
        // If they can, txnAccountID will be unseated, which is not equal to any
        // value.
        if (txnAccountID == accountID)
            return Unexpected("Invalid multisigner.");

        // No duplicate signers allowed.
        if (lastAccountID == accountID)
            return Unexpected("Duplicate Signers not allowed.");

        // Accounts must be in order by account ID.  No duplicates allowed.
        if (lastAccountID > accountID)
            return Unexpected("Unsorted Signers array.");

        // The next signature must be greater than this one.
        lastAccountID = accountID;

        // Verify the signature.
        bool validSig = false;
        std::optional<std::string> errorWhat;
        try
        {
            auto spk = signer.getFieldVL(sfSigningPubKey);
            if (publicKeyType(makeSlice(spk)))
            {
                Blob const signature = signer.getFieldVL(sfTxnSignature);
                validSig = verify(
                    PublicKey(makeSlice(spk)), makeMsg(accountID).slice(), makeSlice(signature));
            }
        }
        catch (std::exception const& e)
        {
            // We assume any problem lies with the signature.
            validSig = false;
            errorWhat = e.what();
        }
        if (!validSig)
        {
            return Unexpected(
                std::string("Invalid signature on account ") + toBase58(accountID) +
                errorWhat.value_or("") + ".");
        }
    }
    // All signatures verified.
    return {};
}

/** Verify a multi-sign batch signature for one entry in `sfBatchSigners`.
 *
 *  Pre-builds the shared batch payload once via `serializeBatch`, then
 *  per-signer appends the signer's `AccountID` via
 *  `finishMultiSigningData`. The owner-cannot-multisign constraint is
 *  skipped (`std::nullopt`) because batch signers are authorizing the
 *  set of inner transactions, not the outer account's own operations.
 *
 *  @param batchSigner The signer sub-object from `sfBatchSigners`.
 *  @param rules       The current ledger rules.
 *  @return An empty `Expected` on success, or an error string.
 */
Expected<void, std::string>
STTx::checkBatchMultiSign(STObject const& batchSigner, Rules const& rules) const
{
    // We can ease the computational load inside the loop a bit by
    // pre-constructing part of the data that we hash.  Fill a Serializer
    // with the stuff that stays constant from signature to signature.
    Serializer dataStart;
    serializeBatch(dataStart, getFlags(), getBatchTransactionIDs());
    return multiSignHelper(
        batchSigner,
        std::nullopt,
        [&dataStart](AccountID const& accountID) -> Serializer {
            Serializer s = dataStart;
            finishMultiSigningData(accountID, s);
            return s;
        },
        rules);
}

/** Verify multi-sign signatures on this transaction or a sub-object.
 *
 *  Pre-builds the shared signing prefix once via
 *  `startMultiSigningData`, then per-signer appends the signer's
 *  `AccountID` via `finishMultiSigningData`. When `sigObject` is
 *  `*this`, the transaction owner's `AccountID` is passed to
 *  `multiSignHelper` to enforce the constraint that the account may not
 *  multisign for themselves; when `sigObject` is a counterparty
 *  sub-object, `std::nullopt` is passed to skip that check.
 *
 *  @param rules     The current ledger rules.
 *  @param sigObject The object containing `sfSigners`. Pass `*this`
 *      for the primary multi-sign check, or a sub-object for a
 *      counterparty check.
 *  @return An empty `Expected` on success, or an error string.
 */
Expected<void, std::string>
STTx::checkMultiSign(Rules const& rules, STObject const& sigObject) const
{
    // Used inside the loop in multiSignHelper to enforce that
    // the account owner may not multisign for themselves.
    auto const txnAccountID =
        &sigObject != this ? std::nullopt : std::optional<AccountID>(getAccountID(sfAccount));

    // We can ease the computational load inside the loop a bit by
    // pre-constructing part of the data that we hash.  Fill a Serializer
    // with the stuff that stays constant from signature to signature.
    Serializer dataStart = startMultiSigningData(*this);
    return multiSignHelper(
        sigObject,
        txnAccountID,
        [&dataStart](AccountID const& accountID) -> Serializer {
            Serializer s = dataStart;
            finishMultiSigningData(accountID, s);
            return s;
        },
        rules);
}

/** Return the cached IDs of the inner transactions in a batch.
 *
 *  On the first call the IDs are computed by hashing each entry in
 *  `sfRawTransactions` and stored in `batchTxnIds_`. Subsequent calls
 *  return the cached vector directly. An assertion on every call
 *  verifies that the cache size still matches `sfRawTransactions`,
 *  enforcing the invariant that inner transactions may not be modified
 *  after the IDs have been observed.
 *
 *  @return A reference to the cached vector of inner transaction IDs.
 *  @note Must only be called on a `ttBATCH` transaction with a
 *      non-empty `sfRawTransactions` array.
 */
std::vector<uint256> const&
STTx::getBatchTransactionIDs() const
{
    XRPL_ASSERT(getTxnType() == ttBATCH, "STTx::getBatchTransactionIDs : not a batch transaction");
    XRPL_ASSERT(
        !getFieldArray(sfRawTransactions).empty(),
        "STTx::getBatchTransactionIDs : empty raw transactions");

    // The list of inner ids is built once, then reused on subsequent calls.
    // After the list is built, it must always have the same size as the array
    // `sfRawTransactions`. The assert below verifies that.
    if (batchTxnIds_.empty())
    {
        for (STObject const& rb : getFieldArray(sfRawTransactions))
            batchTxnIds_.push_back(rb.getHash(HashPrefix::TransactionId));
    }

    XRPL_ASSERT(
        batchTxnIds_.size() == getFieldArray(sfRawTransactions).size(),
        "STTx::getBatchTransactionIDs : batch transaction IDs size mismatch");
    return batchTxnIds_;
}

//------------------------------------------------------------------------------

/** Validate the `sfMemos` field of a transaction object.
 *
 *  Enforces a 1024-byte total serialized memo size and validates that
 *  each `MemoType` and `MemoFormat` field decodes from hex and contains
 *  only RFC 3986 URL-safe characters. `MemoData` is validated for
 *  hex encoding but its decoded content is unrestricted.
 *
 *  The character whitelist is a `constexpr`-initialized 256-element
 *  lookup table, giving O(1) per-character validation without branching
 *  at runtime.
 *
 *  @param st     The transaction object to inspect.
 *  @param reason Populated with a human-readable failure message when
 *      the function returns `false`.
 *  @return `true` if memos are absent or valid; `false` otherwise.
 */
static bool
isMemoOkay(STObject const& st, std::string& reason)
{
    if (!st.isFieldPresent(sfMemos))
        return true;

    auto const& memos = st.getFieldArray(sfMemos);

    // The number 2048 is a preallocation hint, not a hard limit
    // to avoid allocate/copy/free's
    Serializer s(2048);
    memos.add(s);

    // FIXME move the memo limit into a config tunable
    if (s.getDataLength() > 1024)
    {
        reason = "The memo exceeds the maximum allowed size.";
        return false;
    }

    for (auto const& memo : memos)
    {
        auto memoObj = dynamic_cast<STObject const*>(&memo);

        if ((memoObj == nullptr) || (memoObj->getFName() != sfMemo))
        {
            reason = "A memo array may contain only Memo objects.";
            return false;
        }

        for (auto const& memoElement : *memoObj)
        {
            auto const& name = memoElement.getFName();

            if (name != sfMemoType && name != sfMemoData && name != sfMemoFormat)
            {
                reason =
                    "A memo may contain only MemoType, MemoData or "
                    "MemoFormat fields.";
                return false;
            }

            // The raw data is stored as hex-octets, which we want to decode.
            auto optData = strUnHex(memoElement.getText());

            if (!optData)
            {
                reason =
                    "The MemoType, MemoData and MemoFormat fields may "
                    "only contain hex-encoded data.";
                return false;
            }

            if (name == sfMemoData)
                continue;

            // The only allowed characters for MemoType and MemoFormat are the
            // characters allowed in URLs per RFC 3986: alphanumerics and the
            // following symbols: -._~:/?#[]@!$&'()*+,;=%
            static constexpr std::array<char, 256> const kALLOWED_SYMBOLS = []() {
                std::array<char, 256> a{};

                std::string_view const symbols(
                    "0123456789"
                    "-._~:/?#[]@!$&'()*+,;=%"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz");

                for (unsigned char const c : symbols)
                    a[c] = 1;
                return a;
            }();

            for (unsigned char const c : *optData)
            {
                if (kALLOWED_SYMBOLS[c] == 0)
                {
                    reason =
                        "The MemoType and MemoFormat fields may only "
                        "contain characters that are allowed in URLs "
                        "under RFC 3986.";
                    return false;
                }
            }
        }
    }

    return true;
}

/** Verify that no `STAccount` field holds a default (zero) value.
 *
 *  A zero account ID represents an uninitialized field. Any transaction
 *  that contains one must be rejected before submission.
 *
 *  @param st The transaction object to inspect.
 *  @return `true` if all account fields carry non-zero values.
 */
static bool
isAccountFieldOkay(STObject const& st)
{
    for (int i = 0; i < st.getCount(); ++i)
    {
        auto t = dynamic_cast<STAccount const*>(st.peekAtPIndex(i));
        if ((t != nullptr) && t->isDefault())
            return false;
    }

    return true;
}

/** Detect an `MPTIssue` in a field that does not declare MPT support.
 *
 *  Consults the `SOTemplate` for each field's `soeMPTSupported` flag.
 *  Returns `true` if any `STAmount` or `STIssue` field holds an
 *  `MPTIssue` value while the template marks that field as
 *  `SoeMptNone` (not MPT-capable). This catches transactions that try
 *  to use MPT in contexts where it has not been enabled.
 *
 *  @param tx The transaction object to inspect.
 *  @return `true` if an invalid MPT amount is found; `false` otherwise.
 */
static bool
invalidMPTAmountInTx(STObject const& tx)
{
    auto const txType = tx[~sfTransactionType];
    if (!txType)
        return false;
    if (auto const* item = TxFormats::getInstance().findByType(safeCast<TxType>(*txType)))
    {
        for (auto const& e : item->getSOTemplate())
        {
            if (tx.isFieldPresent(e.sField()) && e.supportMPT() != SoeMptNone)
            {
                if (auto const& field = tx.peekAtField(e.sField());
                    (field.getSType() == STI_AMOUNT &&
                     safeDowncast<STAmount const&>(field).holds<MPTIssue>()) ||
                    (field.getSType() == STI_ISSUE &&
                     safeDowncast<STIssue const&>(field).holds<MPTIssue>()))
                {
                    if (e.supportMPT() != SoeMptSupported)
                        return true;
                }
            }
        }
    }
    return false;
}

/** Validate the `sfRawTransactions` array of a batch transaction.
 *
 *  Enforces three constraints:
 *  - The `sfBatchSigners` array (if present) must not exceed
 *    `kMAX_BATCH_TX_COUNT` entries.
 *  - `sfRawTransactions` must not exceed `kMAX_BATCH_TX_COUNT` (8)
 *    entries.
 *  - No inner transaction may itself be of type `ttBATCH` (no
 *    batch-of-batches), and each inner transaction must pass
 *    `applyTemplate` against its registered `SOTemplate`.
 *
 *  @param st     The transaction object to inspect.
 *  @param reason Populated with a human-readable failure message when
 *      the function returns `false`.
 *  @return `true` if `sfRawTransactions` is absent or all entries are
 *      valid; `false` otherwise.
 */
static bool
isRawTransactionOkay(STObject const& st, std::string& reason)
{
    if (!st.isFieldPresent(sfRawTransactions))
        return true;

    if (st.isFieldPresent(sfBatchSigners) &&
        st.getFieldArray(sfBatchSigners).size() > kMAX_BATCH_TX_COUNT)
    {
        reason = "Batch Signers array exceeds max entries.";
        return false;
    }

    auto const& rawTxns = st.getFieldArray(sfRawTransactions);
    if (rawTxns.size() > kMAX_BATCH_TX_COUNT)
    {
        reason = "Raw Transactions array exceeds max entries.";
        return false;
    }
    for (STObject raw : rawTxns)
    {
        try
        {
            TxType const tt = safeCast<TxType>(raw.getFieldU16(sfTransactionType));
            if (tt == ttBATCH)
            {
                reason = "Raw Transactions may not contain batch transactions.";
                return false;
            }

            raw.applyTemplate(getTxFormat(tt)->getSOTemplate());
        }
        catch (std::exception const& e)
        {
            reason = e.what();
            return false;
        }
    }
    return true;
}

/** Run all local pre-submission validity checks on a transaction object.
 *
 *  Gate-keeps local relay and submission by enforcing memo constraints,
 *  non-zero account fields, prohibition of pseudo-transactions, MPT
 *  amount field compatibility, and batch inner-transaction validity.
 *  This is a free function rather than an `STTx` method because it
 *  operates on any `STObject` — it may run before the object is
 *  promoted to a full `STTx`.
 *
 *  @param st     The transaction object to validate.
 *  @param reason Populated with a human-readable failure description
 *      when the function returns `false`.
 *  @return `true` if all checks pass; `false` on the first failure.
 */
bool
passesLocalChecks(STObject const& st, std::string& reason)
{
    if (!isMemoOkay(st, reason))
        return false;

    if (!isAccountFieldOkay(st))
    {
        reason = "An account field is invalid.";
        return false;
    }

    if (isPseudoTx(st))
    {
        reason = "Cannot submit pseudo transactions.";
        return false;
    }

    if (invalidMPTAmountInTx(st))
    {
        reason = "Amount can not be MPT.";
        return false;
    }

    if (!isRawTransactionOkay(st, reason))
        return false;

    return true;
}

/** Canonicalize a transaction via a serialize-then-deserialize round-trip.
 *
 *  Serializes `stx` to bytes via `add()`, then constructs a fresh
 *  `STTx` from those bytes via `SerialIter`. The result is a
 *  wire-canonical transaction where all equivalent in-memory
 *  representations collapse to the same byte sequence, including a
 *  freshly computed transaction ID. Used when a transaction arrives in
 *  a non-canonical form (e.g., built from JSON) and must be stored or
 *  compared against wire-format transactions.
 *
 *  @param stx The source transaction to sterilize.
 *  @return A `shared_ptr` to the newly constructed canonical `STTx`.
 *  @throws std::runtime_error if the round-trip deserialization fails.
 */
std::shared_ptr<STTx const>
sterilize(STTx const& stx)
{
    Serializer s;
    stx.add(s);
    SerialIter sit(s.slice());
    return std::make_shared<STTx const>(std::ref(sit));
}

/** Determine whether a transaction object is a ledger-generated pseudo-transaction.
 *
 *  Pseudo-transactions (`ttAMENDMENT`, `ttFEE`, `ttUNL_MODIFY`) are
 *  synthesized internally by the ledger and must never be submitted by
 *  external clients. `passesLocalChecks` rejects any object for which
 *  this returns `true`.
 *
 *  @param tx The transaction object to test; need not be a fully
 *      constructed `STTx`.
 *  @return `true` if the object carries a pseudo-transaction type.
 */
bool
isPseudoTx(STObject const& tx)
{
    auto const t = tx[~sfTransactionType];

    if (!t)
        return false;

    auto const tt = safeCast<TxType>(*t);

    return tt == ttAMENDMENT || tt == ttFEE || tt == ttUNL_MODIFY;
}

}  // namespace xrpl
