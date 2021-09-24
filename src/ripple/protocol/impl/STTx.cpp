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

#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/basics/contract.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/json/to_string.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/Sign.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/UintTypes.h>
#include <ripple/protocol/jss.h>
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

STTx::STTx(STObject&& object) noexcept(false) : STObject(std::move(object))
{
    tx_type_ = safe_cast<TxType>(getFieldU16(sfTransactionType));
    applyTemplate(getTxFormat(tx_type_)->getSOTemplate());  //  may throw
    tid_ = getHash(HashPrefix::transactionID);
}

STTx::STTx(SerialIter& sit) noexcept(false) : STObject(sfTransaction)
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
            assert(!sacc->isDefault());
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

static Blob
getMultiSigningData(STTx const& that, AccountID const& signingID)
{
    Serializer s;
    s.add32(HashPrefix::txMultiSign);
    that.addWithoutSigningFields(s);
    s.addBitString(signingID);
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

Buffer
STTx::getSignature(PublicKey const& publicKey, SecretKey const& secretKey) const
{
    auto const data = getSigningData(*this);
    return ripple::sign(publicKey, secretKey, makeSlice(data));
}

Buffer
STTx::getMultiSignature(
    AccountID const& signingID,
    PublicKey const& publicKey,
    SecretKey const& secretKey) const
{
    auto const data = getMultiSigningData(*this, signingID);
    return ripple::sign(publicKey, secretKey, makeSlice(data));
}

void
STTx::setSignature(Buffer const& sig)
{
    setFieldVL(sfTxnSignature, sig);
    tid_ = getHash(HashPrefix::transactionID);
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
    setSignature(getSignature(publicKey, secretKey));
}

Expected<void, std::string>
STTx::checkSign(RequireFullyCanonicalSig requireCanonicalSig) const
{
    try
    {
        // Determine whether we're single- or multi-signing by looking
        // at the SigningPubKey.  If it's empty we must be
        // multi-signing.  Otherwise we're single-signing.
        Blob const& signingPubKey = getFieldVL(sfSigningPubKey);
        return signingPubKey.empty() ? checkMultiSign(requireCanonicalSig)
                                     : checkSingleSign(requireCanonicalSig);
    }
    catch (std::exception const&)
    {
    }
    return Unexpected("Internal signature check failure.");
}

Json::Value STTx::getJson(JsonOptions) const
{
    Json::Value ret = STObject::getJson(JsonOptions::none);
    ret[jss::hash] = to_string(getTransactionID());
    return ret;
}

Json::Value
STTx::getJson(JsonOptions options, bool binary) const
{
    if (binary)
    {
        Json::Value ret;
        Serializer s = STObject::getSerializer();
        ret[jss::tx] = strHex(s.peekData());
        ret[jss::hash] = to_string(getTransactionID());
        return ret;
    }
    return getJson(options);
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
    assert(format != nullptr);

    return str(
        boost::format(bfTrans) % to_string(getTransactionID()) %
        format->getName() % toBase58(getAccountID(sfAccount)) %
        getFieldU32(sfSequence) % inLedger % status % rTxn % escapedMetaData);
}

Expected<void, std::string>
STTx::checkSingleSign(RequireFullyCanonicalSig requireCanonicalSig) const
{
    // We don't allow both a non-empty sfSigningPubKey and an sfSigners.
    // That would allow the transaction to be signed two ways.  So if both
    // fields are present the signature is invalid.
    if (isFieldPresent(sfSigners))
        return Unexpected("Cannot both single- and multi-sign.");

    bool validSig = false;
    try
    {
        bool const fullyCanonical = (getFlags() & tfFullyCanonicalSig) ||
            (requireCanonicalSig == RequireFullyCanonicalSig::yes);

        auto const spk = getFieldVL(sfSigningPubKey);

        if (publicKeyType(makeSlice(spk)))
        {
            Blob const signature = getFieldVL(sfTxnSignature);
            Blob const data = getSigningData(*this);

            validSig = verify(
                PublicKey(makeSlice(spk)),
                makeSlice(data),
                makeSlice(signature),
                fullyCanonical);
        }
    }
    catch (std::exception const&)
    {
        // Assume it was a signature failure.
        validSig = false;
    }
    if (validSig == false)
        return Unexpected("Invalid signature.");
    // Signature was verified.
    return {};
}

Expected<void, std::string>
STTx::checkMultiSign(RequireFullyCanonicalSig requireCanonicalSig) const
{
    // Make sure the MultiSigners are present.  Otherwise they are not
    // attempting multi-signing and we just have a bad SigningPubKey.
    if (!isFieldPresent(sfSigners))
        return Unexpected("Empty SigningPubKey.");

    // We don't allow both an sfSigners and an sfTxnSignature.  Both fields
    // being present would indicate that the transaction is signed both ways.
    if (isFieldPresent(sfTxnSignature))
        return Unexpected("Cannot both single- and multi-sign.");

    STArray const& signers{getFieldArray(sfSigners)};

    // There are well known bounds that the number of signers must be within.
    if (signers.size() < minMultiSigners || signers.size() > maxMultiSigners)
        return Unexpected("Invalid Signers array size.");

    // We can ease the computational load inside the loop a bit by
    // pre-constructing part of the data that we hash.  Fill a Serializer
    // with the stuff that stays constant from signature to signature.
    Serializer const dataStart{startMultiSigningData(*this)};

    // We also use the sfAccount field inside the loop.  Get it once.
    auto const txnAccountID = getAccountID(sfAccount);

    // Determine whether signatures must be full canonical.
    bool const fullyCanonical = (getFlags() & tfFullyCanonicalSig) ||
        (requireCanonicalSig == RequireFullyCanonicalSig::yes);

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
            Serializer s = dataStart;
            finishMultiSigningData(accountID, s);

            auto spk = signer.getFieldVL(sfSigningPubKey);

            if (publicKeyType(makeSlice(spk)))
            {
                Blob const signature = signer.getFieldVL(sfTxnSignature);

                validSig = verify(
                    PublicKey(makeSlice(spk)),
                    s.slice(),
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
            static std::array<char, 256> const allowedSymbols = [] {
                std::array<char, 256> a;
                a.fill(0);

                std::string symbols(
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
    auto t = tx[~sfTransactionType];
    if (!t)
        return false;
    auto tt = safe_cast<TxType>(*t);
    return tt == ttAMENDMENT || tt == ttFEE || tt == ttUNL_MODIFY;
}

}  // namespace ripple
