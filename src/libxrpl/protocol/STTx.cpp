//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <xrpl/basics/Log.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/Batch.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/STAccount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/STVector256.h>
#include <xrpl/protocol/Sign.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>
#include <boost/format.hpp>

#include <array>
#include <memory>
#include <type_traits>
#include <utility>

namespace ripple {

static auto
getTxFormat(TxType type)
{
    auto format = TxFormats::getInstance().findByType(type);

    if (format == nullptr)
    {
        Throw<std::runtime_error>(
            "Invalid transaction type " +
            std::to_string(safe_cast<std::underlying_type_t<TxType>>(type)));
    }

    return format;
}

STTx::STTx(STObject&& object) : STObject(std::move(object))
{
    tx_type_ = safe_cast<TxType>(getFieldU16(sfTransactionType));
    applyTemplate(getTxFormat(tx_type_)->getSOTemplate());  //  may throw
    tid_ = getHash(HashPrefix::transactionID);
}

STTx::STTx(SerialIter& sit) : STObject(sfTransaction)
{
    int length = sit.getBytesLeft();

    if ((length < txMinSizeBytes) || (length > txMaxSizeBytes))
        Throw<std::runtime_error>("Transaction length invalid");

    if (set(sit))
        Throw<std::runtime_error>("Transaction contains an object terminator");

    tx_type_ = safe_cast<TxType>(getFieldU16(sfTransactionType));

    applyTemplate(getTxFormat(tx_type_)->getSOTemplate());  // May throw
    tid_ = getHash(HashPrefix::transactionID);
}

STTx::STTx(TxType type, std::function<void(STObject&)> assembler)
    : STObject(sfTransaction)
{
    auto format = getTxFormat(type);

    set(format->getSOTemplate());
    setFieldU16(sfTransactionType, format->getType());

    assembler(*this);

    tx_type_ = safe_cast<TxType>(getFieldU16(sfTransactionType));

    if (tx_type_ != type)
        LogicError("Transaction type was mutated during assembly");

    tid_ = getHash(HashPrefix::transactionID);
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
            XRPL_ASSERT(
                !sacc->isDefault(),
                "ripple::STTx::getMentionedAccounts : account is set");
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
    s.add32(HashPrefix::txSign);
    that.addWithoutSigningFields(s);
    return s.getData();
}

uint256
STTx::getSigningHash() const
{
    return STObject::getSigningHash(HashPrefix::txSign);
}

Blob
STTx::getSignature() const
{
    try
    {
        return getFieldVL(sfTxnSignature);
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
        // No TicketSequence specified.  Return the Sequence, whatever it is.
        return SeqProxy::sequence(seq);

    return SeqProxy{SeqProxy::ticket, *ticketSeq};
}

void
STTx::sign(PublicKey const& publicKey, SecretKey const& secretKey)
{
    auto const data = getSigningData(*this);

    auto const sig = ripple::sign(publicKey, secretKey, makeSlice(data));

    setFieldVL(sfTxnSignature, sig);
    tid_ = getHash(HashPrefix::transactionID);
}

Expected<void, std::string>
STTx::checkSign(
    RequireFullyCanonicalSig requireCanonicalSig,
    Rules const& rules) const
{
    try
    {
        // Determine whether we're single- or multi-signing by looking
        // at the SigningPubKey.  If it's empty we must be
        // multi-signing.  Otherwise we're single-signing.
        Blob const& signingPubKey = getFieldVL(sfSigningPubKey);
        return signingPubKey.empty()
            ? checkMultiSign(requireCanonicalSig, rules)
            : checkSingleSign(requireCanonicalSig);
    }
    catch (std::exception const&)
    {
    }
    return Unexpected("Internal signature check failure.");
}

Expected<void, std::string>
STTx::checkBatchSign(
    RequireFullyCanonicalSig requireCanonicalSig,
    Rules const& rules) const
{
    try
    {
        STArray const& signers{getFieldArray(sfBatchSigners)};
        for (auto const& signer : signers)
        {
            Blob const& signingPubKey = signer.getFieldVL(sfSigningPubKey);
            auto const result = signingPubKey.empty()
                ? checkBatchMultiSign(signer, requireCanonicalSig, rules)
                : checkBatchSingleSign(signer, requireCanonicalSig);

            if (!result)
                return result;
        }
        return {};
    }
    catch (std::exception const&)
    {
    }
    return Unexpected("Internal signature check failure.");
}

Json::Value
STTx::getJson(JsonOptions options) const
{
    Json::Value ret = STObject::getJson(JsonOptions::none);
    if (!(options & JsonOptions::disable_API_prior_V2))
        ret[jss::hash] = to_string(getTransactionID());
    return ret;
}

Json::Value
STTx::getJson(JsonOptions options, bool binary) const
{
    bool const V1 = !(options & JsonOptions::disable_API_prior_V2);

    if (binary)
    {
        Serializer s = STObject::getSerializer();
        std::string const dataBin = strHex(s.peekData());

        if (V1)
        {
            Json::Value ret(Json::objectValue);
            ret[jss::tx] = dataBin;
            ret[jss::hash] = to_string(getTransactionID());
            return ret;
        }
        else
            return Json::Value{dataBin};
    }

    Json::Value ret = STObject::getJson(JsonOptions::none);
    if (V1)
        ret[jss::hash] = to_string(getTransactionID());

    return ret;
}

std::string const&
STTx::getMetaSQLInsertReplaceHeader()
{
    static std::string const sql =
        "INSERT OR REPLACE INTO Transactions "
        "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, "
        "TxnMeta)"
        " VALUES ";

    return sql;
}

std::string
STTx::getMetaSQL(std::uint32_t inLedger, std::string const& escapedMetaData)
    const
{
    Serializer s;
    add(s);
    return getMetaSQL(s, inLedger, txnSqlValidated, escapedMetaData);
}

// VFALCO This could be a free function elsewhere
std::string
STTx::getMetaSQL(
    Serializer rawTxn,
    std::uint32_t inLedger,
    char status,
    std::string const& escapedMetaData) const
{
    static boost::format bfTrans(
        "('%s', '%s', '%s', '%d', '%d', '%c', %s, %s)");
    std::string rTxn = sqlBlobLiteral(rawTxn.peekData());

    auto format = TxFormats::getInstance().findByType(tx_type_);
    XRPL_ASSERT(format, "ripple::STTx::getMetaSQL : non-null type format");

    return str(
        boost::format(bfTrans) % to_string(getTransactionID()) %
        format->getName() % toBase58(getAccountID(sfAccount)) %
        getFieldU32(sfSequence) % inLedger % status % rTxn % escapedMetaData);
}

static Expected<void, std::string>
singleSignHelper(
    STObject const& signer,
    Slice const& data,
    bool const fullyCanonical)
{
    // We don't allow both a non-empty sfSigningPubKey and an sfSigners.
    // That would allow the transaction to be signed two ways.  So if both
    // fields are present the signature is invalid.
    if (signer.isFieldPresent(sfSigners))
        return Unexpected("Cannot both single- and multi-sign.");

    bool validSig = false;
    try
    {
        auto const spk = signer.getFieldVL(sfSigningPubKey);
        if (publicKeyType(makeSlice(spk)))
        {
            Blob const signature = signer.getFieldVL(sfTxnSignature);
            validSig = verify(
                PublicKey(makeSlice(spk)),
                data,
                makeSlice(signature),
                fullyCanonical);
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

Expected<void, std::string>
STTx::checkSingleSign(RequireFullyCanonicalSig requireCanonicalSig) const
{
    auto const data = getSigningData(*this);
    bool const fullyCanonical = (getFlags() & tfFullyCanonicalSig) ||
        (requireCanonicalSig == STTx::RequireFullyCanonicalSig::yes);
    return singleSignHelper(*this, makeSlice(data), fullyCanonical);
}

Expected<void, std::string>
STTx::checkBatchSingleSign(
    STObject const& batchSigner,
    RequireFullyCanonicalSig requireCanonicalSig) const
{
    Serializer msg;
    serializeBatch(msg, getFlags(), getBatchTransactionIDs());
    bool const fullyCanonical = (getFlags() & tfFullyCanonicalSig) ||
        (requireCanonicalSig == STTx::RequireFullyCanonicalSig::yes);
    return singleSignHelper(batchSigner, msg.slice(), fullyCanonical);
}

Expected<void, std::string>
multiSignHelper(
    STObject const& signerObj,
    bool const fullyCanonical,
    std::function<std::vector<uint8_t>(AccountID const&)> makeMsg,
    Rules const& rules)
{
    // Make sure the MultiSigners are present.  Otherwise they are not
    // attempting multi-signing and we just have a bad SigningPubKey.
    if (!signerObj.isFieldPresent(sfSigners))
        return Unexpected("Empty SigningPubKey.");

    // We don't allow both an sfSigners and an sfTxnSignature.  Both fields
    // being present would indicate that the transaction is signed both ways.
    if (signerObj.isFieldPresent(sfTxnSignature))
        return Unexpected("Cannot both single- and multi-sign.");

    STArray const& signers{signerObj.getFieldArray(sfSigners)};

    // There are well known bounds that the number of signers must be within.
    if (signers.size() < STTx::minMultiSigners ||
        signers.size() > STTx::maxMultiSigners(&rules))
        return Unexpected("Invalid Signers array size.");

    // We also use the sfAccount field inside the loop.  Get it once.
    auto const txnAccountID = signerObj.getAccountID(sfAccount);

    // Signers must be in sorted order by AccountID.
    AccountID lastAccountID(beast::zero);

    for (auto const& signer : signers)
    {
        auto const accountID = signer.getAccountID(sfAccount);

        // The account owner may not multisign for themselves.
        if (accountID == txnAccountID)
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
        try
        {
            std::vector<uint8_t> msgData = makeMsg(accountID);
            Slice msgSlice(msgData.data(), msgData.size());
            auto spk = signer.getFieldVL(sfSigningPubKey);

            if (publicKeyType(makeSlice(spk)))
            {
                Blob const signature = signer.getFieldVL(sfTxnSignature);
                validSig = verify(
                    PublicKey(makeSlice(spk)),
                    msgSlice,
                    makeSlice(signature),
                    fullyCanonical);
            }
        }
        catch (std::exception const&)
        {
            // We assume any problem lies with the signature.
            validSig = false;
        }
        if (!validSig)
            return Unexpected(
                std::string("Invalid signature on account ") +
                toBase58(accountID) + ".");
    }
    // All signatures verified.
    return {};
}

Expected<void, std::string>
STTx::checkBatchMultiSign(
    STObject const& batchSigner,
    RequireFullyCanonicalSig requireCanonicalSig,
    Rules const& rules) const
{
    Serializer msg;
    serializeBatch(msg, getFlags(), getBatchTransactionIDs());
    bool const fullyCanonical = (getFlags() & tfFullyCanonicalSig) ||
        (requireCanonicalSig == RequireFullyCanonicalSig::yes);

    return multiSignHelper(
        batchSigner,
        fullyCanonical,
        [&msg](AccountID const&) -> std::vector<uint8_t> {
            return msg.getData();
        },
        rules);
}

Expected<void, std::string>
STTx::checkMultiSign(
    RequireFullyCanonicalSig requireCanonicalSig,
    Rules const& rules) const
{
    bool const fullyCanonical = (getFlags() & tfFullyCanonicalSig) ||
        (requireCanonicalSig == RequireFullyCanonicalSig::yes);
    return multiSignHelper(
        *this,
        fullyCanonical,
        [this](AccountID const& accountID) -> std::vector<uint8_t> {
            Serializer dataStart = startMultiSigningData(*this);
            finishMultiSigningData(accountID, dataStart);
            return dataStart.getData();
        },
        rules);
}

/**
 * @brief Retrieves a batch of transaction IDs from the STTx.
 *
 * This function returns a vector of transaction IDs by extracting them from
 * the field array `sfRawTransactions` within the STTx. If the batch
 * transaction IDs have already been computed and cached in `batch_txn_ids_`,
 * it returns the cached vector. Otherwise, it computes the transaction IDs,
 * caches them, and then returns the vector.
 *
 * @return A vector of `uint256` containing the batch transaction IDs.
 *
 * @note The function asserts that the `sfRawTransactions` field array is not
 * empty and that the size of the computed batch transaction IDs matches the
 * size of the `sfRawTransactions` field array.
 */
std::vector<uint256>
STTx::getBatchTransactionIDs() const
{
    XRPL_ASSERT(
        getFieldArray(sfRawTransactions).size() != 0,
        "Batch transaction missing sfRawTransactions");
    if (batch_txn_ids_.size() != 0)
        return batch_txn_ids_;

    for (STObject const& rb : getFieldArray(sfRawTransactions))
        batch_txn_ids_.push_back(rb.getHash(HashPrefix::transactionID));

    XRPL_ASSERT(
        batch_txn_ids_.size() == getFieldArray(sfRawTransactions).size(),
        "hashes array size does not match txns");
    return batch_txn_ids_;
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

        if (!memoObj || (memoObj->getFName() != sfMemo))
        {
            reason = "A memo array may contain only Memo objects.";
            return false;
        }

        for (auto const& memoElement : *memoObj)
        {
            auto const& name = memoElement.getFName();

            if (name != sfMemoType && name != sfMemoData &&
                name != sfMemoFormat)
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
            static constexpr std::array<char, 256> const allowedSymbols = []() {
                std::array<char, 256> a{};

                std::string_view symbols(
                    "0123456789"
                    "-._~:/?#[]@!$&'()*+,;=%"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz");

                for (char c : symbols)
                    a[c] = 1;
                return a;
            }();

            for (auto c : *optData)
            {
                if (!allowedSymbols[c])
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
        if (t && t->isDefault())
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
    if (auto const* item =
            TxFormats::getInstance().findByType(safe_cast<TxType>(*txType)))
    {
        for (auto const& e : item->getSOTemplate())
        {
            if (tx.isFieldPresent(e.sField()) && e.supportMPT() != soeMPTNone)
            {
                if (auto const& field = tx.peekAtField(e.sField());
                    (field.getSType() == STI_AMOUNT &&
                     static_cast<STAmount const&>(field).holds<MPTIssue>()) ||
                    (field.getSType() == STI_ISSUE &&
                     static_cast<STIssue const&>(field).holds<MPTIssue>()))
                {
                    if (e.supportMPT() != soeMPTSupported)
                        return true;
                }
            }
        }
    }
    return false;
}

static bool
isRawTransactionOkay(STObject const& st, std::string& reason)
{
    if (!st.isFieldPresent(sfRawTransactions))
        return true;

    auto const& rawTxns = st.getFieldArray(sfRawTransactions);
    for (STObject raw : rawTxns)
    {
        if (!raw.isFieldPresent(sfTransactionType))
        {
            reason = "Field 'TransactionType' is required but missing.";
            return false;
        }

        try
        {
            TxType const tt =
                safe_cast<TxType>(raw.getFieldU16(sfTransactionType));
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

    auto const tt = safe_cast<TxType>(*t);

    return tt == ttAMENDMENT || tt == ttFEE || tt == ttUNL_MODIFY;
}

}  // namespace ripple
