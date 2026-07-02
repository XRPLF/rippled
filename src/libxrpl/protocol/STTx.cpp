#include <xrpl/protocol/STTx.h>

#include <xrpl/basics/Blob.h>
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
#include <expected>
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

STTx::STTx(STObject&& object) : STObject(std::move(object))
{
    txType_ = safeCast<TxType>(getFieldU16(sfTransactionType));
    applyTemplate(getTxFormat(txType_)->getSOTemplate());  //  may throw
    tid_ = getHash(HashPrefix::TransactionId);
    buildBatchTxnIds();
}

STTx::STTx(SerialIter& sit) : STObject(sfTransaction)
{
    int const length = sit.getBytesLeft();

    if ((length < kTxMinSizeBytes) || (length > kTxMaxSizeBytes))
        Throw<std::runtime_error>("Transaction length invalid");

    if (set(sit))
        Throw<std::runtime_error>("Transaction contains an object terminator");

    txType_ = safeCast<TxType>(getFieldU16(sfTransactionType));

    applyTemplate(getTxFormat(txType_)->getSOTemplate());  // May throw
    tid_ = getHash(HashPrefix::TransactionId);
    buildBatchTxnIds();
}

STTx::STTx(TxType type, std::function<void(STObject&)> assembler) : STObject(sfTransaction)
{
    auto format = getTxFormat(type);

    set(format->getSOTemplate());
    setFieldU16(sfTransactionType, format->getType());

    assembler(*this);

    txType_ = safeCast<TxType>(getFieldU16(sfTransactionType));

    if (txType_ != type)
        logicError("Transaction type was mutated during assembly");

    tid_ = getHash(HashPrefix::TransactionId);
    buildBatchTxnIds();
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

std::expected<void, std::string>
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
        return std::unexpected("Internal signature check failure.");
    }
}

std::expected<void, std::string>
STTx::checkSign(Rules const& rules) const
{
    if (auto const ret = checkSign(rules, *this); !ret)
        return ret;

    if (isFieldPresent(sfCounterpartySignature))
    {
        auto const counterSig = getFieldObject(sfCounterpartySignature);
        if (auto const ret = checkSign(rules, counterSig); !ret)
            return std::unexpected("Counterparty: " + ret.error());
    }

    // Verify the batch signer signatures here too, so they are cached with the
    // rest of signature checking (checkValidity / SF_SIGGOOD) and stay out of
    // the transaction engine. Gated on a batch (batchTxnIds_ seated) that
    // actually carries signers; a batch whose inners are all from the outer
    // account has no sfBatchSigners and needs no signer crypto.
    if (batchTxnIds_ && isFieldPresent(sfBatchSigners))
    {
        if (auto const ret = checkBatchSign(rules); !ret)
            return ret;
    }
    return {};
}

std::expected<void, std::string>
STTx::checkBatchSign(Rules const& rules) const
{
    try
    {
        if (getTxnType() != ttBATCH)
        {
            // LCOV_EXCL_START
            UNREACHABLE("STTx::checkBatchSign : not a batch transaction");
            return std::unexpected("Not a batch transaction.");
            // LCOV_EXCL_STOP
        }
        if (!isFieldPresent(sfBatchSigners))
            return std::unexpected("Missing BatchSigners field.");  // LCOV_EXCL_LINE
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
        // LCOV_EXCL_START
        return std::unexpected(std::string("Internal batch signature check failure: ") + e.what());
        // LCOV_EXCL_STOP
    }
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

std::string const&
STTx::getMetaSQLInsertReplaceHeader()
{
    static std::string const kSql =
        "INSERT OR REPLACE INTO Transactions "
        "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, "
        "TxnMeta)"
        " VALUES ";

    return kSql;
}

std::string
STTx::getMetaSQL(std::uint32_t inLedger, std::string const& escapedMetaData) const
{
    Serializer s;
    add(s);
    return getMetaSQL(s, inLedger, TxnSql::Validated, escapedMetaData);
}

// VFALCO This could be a free function elsewhere
std::string
STTx::getMetaSQL(
    Serializer rawTxn,
    std::uint32_t inLedger,
    TxnSql status,
    std::string const& escapedMetaData) const
{
    static boost::format const kBfTrans("('%s', '%s', '%s', '%d', '%d', '%c', %s, %s)");
    std::string rTxn = sqlBlobLiteral(rawTxn.peekData());

    auto format = TxFormats::getInstance().findByType(txType_);
    XRPL_ASSERT(format, "xrpl::STTx::getMetaSQL : non-null type format");

    return str(
        boost::format(kBfTrans) % to_string(getTransactionID()) % format->getName() %
        toBase58(getAccountID(sfAccount)) % getFieldU32(sfSequence) % inLedger %
        safeCast<char>(status) % rTxn % escapedMetaData);
}

static std::expected<void, std::string>
singleSignHelper(STObject const& sigObject, Slice const& data)
{
    // We don't allow both a non-empty sfSigningPubKey and an sfSigners.
    // That would allow the transaction to be signed two ways.  So if both
    // fields are present the signature is invalid.
    if (sigObject.isFieldPresent(sfSigners))
        return std::unexpected("Cannot both single- and multi-sign.");

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
        return std::unexpected("Invalid signature.");

    return {};
}

std::expected<void, std::string>
STTx::checkSingleSign(STObject const& sigObject) const
{
    auto const data = getSigningData(*this);
    return singleSignHelper(sigObject, makeSlice(data));
}

std::expected<void, std::string>
STTx::checkBatchSingleSign(STObject const& batchSigner) const
{
    XRPL_ASSERT(getTxnType() == ttBATCH, "STTx::checkBatchSingleSign : batch transaction");
    Serializer msg;
    serializeBatch(
        msg, getAccountID(sfAccount), getSeqValue(), getFlags(), getBatchTransactionIDs());
    finishMultiSigningData(batchSigner.getAccountID(sfAccount), msg);
    return singleSignHelper(batchSigner, msg.slice());
}

std::expected<void, std::string>
multiSignHelper(
    STObject const& sigObject,
    std::optional<AccountID> txnAccountID,
    std::function<Serializer(AccountID const&)> makeMsg,
    Rules const& rules)
{
    // Make sure the MultiSigners are present.  Otherwise they are not
    // attempting multi-signing and we just have a bad SigningPubKey.
    if (!sigObject.isFieldPresent(sfSigners))
        return std::unexpected("Empty SigningPubKey.");

    // We don't allow both an sfSigners and an sfTxnSignature.  Both fields
    // being present would indicate that the transaction is signed both ways.
    if (sigObject.isFieldPresent(sfTxnSignature))
        return std::unexpected("Cannot both single- and multi-sign.");

    STArray const& signers{sigObject.getFieldArray(sfSigners)};

    // There are well known bounds that the number of signers must be within.
    if (signers.size() < STTx::kMinMultiSigners || signers.size() > STTx::kMaxMultiSigners)
        return std::unexpected("Invalid Signers array size.");

    // Signers must be in sorted order by AccountID.
    AccountID lastAccountID(beast::kZero);

    for (auto const& signer : signers)
    {
        auto const accountID = signer.getAccountID(sfAccount);

        // The account owner may not usually multisign for themselves.
        // If they can, txnAccountID will be unseated, which is not equal to any
        // value.
        if (txnAccountID == accountID)
            return std::unexpected("Invalid multisigner.");

        // No duplicate signers allowed.
        if (lastAccountID == accountID)
            return std::unexpected("Duplicate Signers not allowed.");

        // Accounts must be in order by account ID.  No duplicates allowed.
        if (lastAccountID > accountID)
            return std::unexpected("Unsorted Signers array.");

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
            return std::unexpected(
                std::string("Invalid signature on account ") + toBase58(accountID) +
                (errorWhat ? ": " + *errorWhat : "") + ".");
        }
    }
    // All signatures verified.
    return {};
}

std::expected<void, std::string>
STTx::checkBatchMultiSign(STObject const& batchSigner, Rules const& rules) const
{
    XRPL_ASSERT(getTxnType() == ttBATCH, "STTx::checkBatchMultiSign : batch transaction");
    // We can ease the computational load inside the loop a bit by
    // pre-constructing part of the data that we hash.  Fill a Serializer
    // with the stuff that stays constant from signature to signature.
    auto const batchSignerAccount = batchSigner.getAccountID(sfAccount);
    Serializer dataStart;
    serializeBatch(
        dataStart, getAccountID(sfAccount), getSeqValue(), getFlags(), getBatchTransactionIDs());
    dataStart.addBitString(batchSignerAccount);
    return multiSignHelper(
        batchSigner,
        batchSignerAccount,
        [&dataStart](AccountID const& accountID) -> Serializer {
            Serializer s = dataStart;
            finishMultiSigningData(accountID, s);
            return s;
        },
        rules);
}

std::expected<void, std::string>
STTx::checkMultiSign(Rules const& rules, STObject const& sigObject) const
{
    // Used inside the loop in multiSignHelper to enforce that
    // the account owner may not multisign for themselves.
    // For delegated transactions sfDelegate is the account whose signer list is checked,
    // the delegate account itself can not be among the signers.
    auto const txnAccountID =
        &sigObject != this ? std::nullopt : std::optional<AccountID>(getFeePayer());

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

void
STTx::buildBatchTxnIds()
{
    // Precondition: the template must have been applied first, so the fields
    // (including sfRawTransactions) are canonical before the inner txns are
    // hashed. The constructors call this immediately after applying the
    // template; isFree() being false confirms a template is set.
    XRPL_ASSERT(!isFree(), "STTx::buildBatchTxnIds : template applied");
    if (getTxnType() != ttBATCH || !isFieldPresent(sfRawTransactions))
        return;

    auto const& raw = getFieldArray(sfRawTransactions);

    // Seated for any batch with raw transactions. The count is validated in
    // preflight and at the relay boundary, so build every id here; this keeps
    // the invariant batchTxnIds_->size() == rawTransactions.size().
    auto& ids = batchTxnIds_.emplace();
    ids.reserve(raw.size());
    for (STObject const& rb : raw)
        ids.push_back(rb.getHash(HashPrefix::TransactionId));
}

std::vector<uint256> const&
STTx::getBatchTransactionIDs() const
{
    XRPL_ASSERT(getTxnType() == ttBATCH, "STTx::getBatchTransactionIDs : batch transaction");
    XRPL_ASSERT(
        batchTxnIds_.has_value(), "STTx::getBatchTransactionIDs : batch transaction IDs built");
    XRPL_ASSERT(
        batchTxnIds_->size() == getFieldArray(sfRawTransactions).size(),
        "STTx::getBatchTransactionIDs : batch transaction IDs size mismatch");
    return *batchTxnIds_;
}

//------------------------------------------------------------------------------

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
            static constexpr std::array<char, 256> const kAllowedSymbols = []() {
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
                if (kAllowedSymbols[c] == 0)
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

// Ensure all account fields are 160-bits
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

static bool
isBatchRawTransactionOkay(STObject const& st, std::string& reason)
{
    if (!st.isFieldPresent(sfRawTransactions))
        return true;

    // sfRawTransactions only appears on a Batch. passesLocalChecks runs on
    // unverified user and peer input, so reject (rather than assert) a non-batch
    // transaction that carries it.
    if (st.getFieldU16(sfTransactionType) != ttBATCH)
    {
        reason = "Only Batch transactions may contain raw transactions.";
        return false;
    }

    if (st.isFieldPresent(sfBatchSigners) &&
        st.getFieldArray(sfBatchSigners).size() > kMaxBatchSigners)
    {
        reason = "Batch Signers array exceeds max entries.";
        return false;
    }

    auto const& rawTxns = st.getFieldArray(sfRawTransactions);
    if (rawTxns.size() > kMaxBatchTxCount)
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

            // passesLocalChecks recurses back into isBatchRawTransactionOkay,
            // but an inner can never be a batch (rejected above), so the
            // recursion terminates at depth 1.
            if (!passesLocalChecks(raw, reason))
                return false;
        }
        catch (std::exception const& e)
        {
            reason = e.what();
            return false;
        }
    }
    return true;
}

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

    if (!isBatchRawTransactionOkay(st, reason))
        return false;

    return true;
}

std::shared_ptr<STTx const>
sterilize(STTx const& stx)
{
    Serializer s;
    stx.add(s);
    SerialIter sit(s.slice());
    return std::make_shared<STTx const>(std::ref(sit));
}

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
