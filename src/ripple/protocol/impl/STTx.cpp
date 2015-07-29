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

#include <BeastConfig.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/types.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/to_string.h>
#include <beast/unit_test/suite.h>
#include <beast/cxx14/memory.h> // <memory>
#include <boost/format.hpp>
#include <array>

namespace ripple {

STTx::STTx (TxType type)
    : STObject (sfTransaction)
    , tx_type_ (type)
{
    auto format = TxFormats::getInstance().findByType (type);

    if (format == nullptr)
    {
        WriteLog (lsWARNING, STTx) <<
            "Transaction type: " << type;
        throw std::runtime_error ("invalid transaction type");
    }

    set (format->elements);
    setFieldU16 (sfTransactionType, format->getType ());
}

STTx::STTx (STObject&& object)
    : STObject (std::move (object))
{
    tx_type_ = static_cast <TxType> (getFieldU16 (sfTransactionType));

    auto format = TxFormats::getInstance().findByType (tx_type_);

    if (!format)
    {
        WriteLog (lsWARNING, STTx) <<
            "Transaction type: " << tx_type_;
        throw std::runtime_error ("invalid transaction type");
    }

    if (!setType (format->elements))
    {
        WriteLog (lsWARNING, STTx) <<
            "Transaction not legal for format";
        throw std::runtime_error ("transaction not valid");
    }
}

STTx::STTx (SerialIter& sit)
    : STObject (sfTransaction)
{
    int length = sit.getBytesLeft ();

    if ((length < Protocol::txMinSizeBytes) || (length > Protocol::txMaxSizeBytes))
    {
        WriteLog (lsERROR, STTx) <<
            "Transaction has invalid length: " << length;
        throw std::runtime_error ("Transaction length invalid");
    }

    set (sit);
    tx_type_ = static_cast<TxType> (getFieldU16 (sfTransactionType));

    auto format = TxFormats::getInstance().findByType (tx_type_);

    if (!format)
    {
        WriteLog (lsWARNING, STTx) <<
            "Invalid transaction type: " << tx_type_;
        throw std::runtime_error ("invalid transaction type");
    }

    if (!setType (format->elements))
    {
        WriteLog (lsWARNING, STTx) <<
            "Transaction not legal for format";
        throw std::runtime_error ("transaction not valid");
    }
}

std::string
STTx::getFullText () const
{
    std::string ret = "\"";
    ret += to_string (getTransactionID ());
    ret += "\" = {";
    ret += STObject::getFullText ();
    ret += "}";
    return ret;
}

boost::container::flat_set<AccountID>
STTx::getMentionedAccounts () const
{
    boost::container::flat_set<AccountID> list;

    for (auto const& it : *this)
    {
        if (auto sa = dynamic_cast<STAccount const*> (&it))
        {
            AccountID id;
            assert(sa->isValueH160());
            if (sa->getValueH160(id))
                list.insert(id);
        }
        else if (auto sa = dynamic_cast<STAmount const*> (&it))
        {
            auto const& issuer = sa->getIssuer ();
            if (! isXRP (issuer))
                list.insert(issuer);
        }
    }

    return list;
}

static Blob getSigningData (STTx const& that)
{
    Serializer s;
    s.add32 (HashPrefix::txSign);
    that.addWithoutSigningFields (s);
    return s.getData();
}

uint256
STTx::getSigningHash () const
{
    return STObject::getSigningHash (HashPrefix::txSign);
}

uint256
STTx::getTransactionID () const
{
    return getHash (HashPrefix::transactionID);
}

Blob STTx::getSignature () const
{
    try
    {
        return getFieldVL (sfTxnSignature);
    }
    catch (...)
    {
        return Blob ();
    }
}

void STTx::sign (RippleAddress const& private_key)
{
    Blob const signature = private_key.accountPrivateSign (getSigningData (*this));
    setFieldVL (sfTxnSignature, signature);
}

bool STTx::checkSign(bool allowMultiSign) const
{
    bool sigGood = false;
    try
    {
        if (allowMultiSign)
        {
            // Determine whether we're single- or multi-signing by looking
            // at the SigningPubKey.  It it's empty we must be multi-signing.
            // Otherwise we're single-signing.
            Blob const& signingPubKey = getFieldVL (sfSigningPubKey);
            sigGood = signingPubKey.empty () ?
                checkMultiSign () : checkSingleSign ();
        }
        else
        {
            sigGood = checkSingleSign ();
        }
    }
    catch (...)
    {
    }
    return sigGood;
}

void STTx::setSigningPubKey (RippleAddress const& naSignPubKey)
{
    setFieldVL (sfSigningPubKey, naSignPubKey.getAccountPublic ());
}

Json::Value STTx::getJson (int) const
{
    Json::Value ret = STObject::getJson (0);
    ret[jss::hash] = to_string (getTransactionID ());
    return ret;
}

Json::Value STTx::getJson (int options, bool binary) const
{
    if (binary)
    {
        Json::Value ret;
        Serializer s = STObject::getSerializer ();
        ret[jss::tx] = strHex (s.peekData ());
        ret[jss::hash] = to_string (getTransactionID ());
        return ret;
    }
    return getJson(options);
}

std::string const&
STTx::getMetaSQLInsertReplaceHeader ()
{
    static std::string const sql = "INSERT OR REPLACE INTO Transactions "
        "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, TxnMeta)"
        " VALUES ";

    return sql;
}

std::string STTx::getMetaSQL (std::uint32_t inLedger,
                                               std::string const& escapedMetaData) const
{
    Serializer s;
    add (s);
    return getMetaSQL (s, inLedger, TXN_SQL_VALIDATED, escapedMetaData);
}

// VFALCO This could be a free function elsewhere
std::string
STTx::getMetaSQL (Serializer rawTxn,
    std::uint32_t inLedger, char status, std::string const& escapedMetaData) const
{
    static boost::format bfTrans ("('%s', '%s', '%s', '%d', '%d', '%c', %s, %s)");
    std::string rTxn = sqlEscape (rawTxn.peekData ());

    auto format = TxFormats::getInstance().findByType (tx_type_);
    assert (format != nullptr);

    return str (boost::format (bfTrans)
                % to_string (getTransactionID ()) % format->getName ()
                % toBase58(getAccountID(sfAccount))
                % getSequence () % inLedger % status % rTxn % escapedMetaData);
}

bool
STTx::checkSingleSign () const
{
    // We don't allow both a non-empty sfSigningPubKey and an sfMultiSigners.
    // That would allow the transaction to be signed two ways.  So if both
    // fields are present the signature is invalid.
    if (isFieldPresent (sfMultiSigners))
        return false;

    bool ret = false;
    try
    {
        ECDSA const fullyCanonical = (getFlags() & tfFullyCanonicalSig)
            ? ECDSA::strict
            : ECDSA::not_strict;

        RippleAddress n;
        n.setAccountPublic (getFieldVL (sfSigningPubKey));

        ret = n.accountPublicVerify (getSigningData (*this),
            getFieldVL (sfTxnSignature), fullyCanonical);
    }
    catch (...)
    {
        // Assume it was a signature failure.
        ret = false;
    }
    return ret;
}

bool
STTx::checkMultiSign () const
{
    // Make sure the MultiSigners are present.  Otherwise they are not
    // attempting multi-signing and we just have a bad SigningPubKey.
    if (!isFieldPresent (sfMultiSigners))
        return false;

    STArray const& multiSigners (getFieldArray (sfMultiSigners));

    // There are well known bounds that the number of signers must be within.
    {
        std::size_t const multiSignerCount = multiSigners.size ();
        if ((multiSignerCount < 1) || (multiSignerCount > maxMultiSigners))
            return false;
    }

    // We can ease the computational load inside the loop a bit by
    // pre-constructing part of the data that we hash.  Fill a Serializer
    // with the stuff that stays constant from signature to signature.
    Serializer const dataStart (startMultiSigningData ());

    // We also use the sfAccount field inside the loop.  Get it once.
    auto const txnAccountID = getAccountID (sfAccount);

    // Determine whether signatures must be full canonical.
    ECDSA const fullyCanonical = (getFlags() & tfFullyCanonicalSig)
        ? ECDSA::strict
        : ECDSA::not_strict;

    /*
        We need to detect (and reject) if a multi-signer is both signing
        directly and using a SigningFor.  Here's an example:
        
        {
            ...
            "MultiSigners": [
                {
                    "SigningFor": {
                        "Account": "<alice>",
                        "SigningAccounts": [
                            {
                                "SigningAccount": {
                                    // * becky says that becky signs for alice. *
                                    "Account": "<becky>",
            ...
                    "SigningFor": {
                        "Account": "<becky>",
                        "SigningAccounts": [
                            {
                                "SigningAccount": {
                                    // * cheri says that becky signs for alice. *
                                    "Account": "<cheri>",
            ...
            "tx_json": {
                "Account": "<alice>",
                ...
            }
        }
        
        Why is this way of signing a problem?  Alice has a signer list, and
        Becky can show up in that list only once.  By design.  So if Becky
        signs twice -- once directly and once indirectly -- we have three
        options:
        
         1. We can add Becky's weight toward Alice's quorum twice, once for
            each signature.  This seems both unexpected and counter to Alice's
            intention.
        
         2. We could allow both signatures, but only add Becky's weight
            toward Alice's quorum once.  This seems a bit better.  But it allows
            our clients to ask rippled to do more work than necessary.  We
            should also let the client know that only one of the signatures
            was necessary.
        
         3. The only way to tell the client that they have done more work
            than necessary (and that one of the signatures will be ignored) is
            to declare the transaction malformed.  This behavior also aligns
            well with rippled's behavior if Becky had signed directly twice:
            the transaction would be marked as malformed.
    */

    // We use this std::set to detect this form of double-signing.
    std::set<AccountID> firstLevelSigners;

    // SigningFors must be in sorted order by AccountID.
    AccountID lastSigningForID = zero;

    // Every signature must verify or we reject the transaction.
    for (auto const& signingFor : multiSigners)
    {
        auto const signingForID =
            signingFor.getAccountID (sfAccount);

        // SigningFors must be in order by account ID.  No duplicates allowed.
        if (lastSigningForID >= signingForID)
            return false;

        // The next SigningFor must be greater than this one.
        lastSigningForID = signingForID;

        // If signingForID is *not* txnAccountID, then look for duplicates.
        bool const directSigning = (signingForID == txnAccountID);
        if (! directSigning)
        {
            if (! firstLevelSigners.insert (signingForID).second)
                // This is a duplicate signer.  Fail.
                return false;
        }

        STArray const& signingAccounts (
            signingFor.getFieldArray (sfSigningAccounts));

        // There are bounds that the number of signers must be within.
        {
            std::size_t const signingAccountsCount = signingAccounts.size ();
            if ((signingAccountsCount < 1) ||
                (signingAccountsCount > maxMultiSigners))
            {
                return false;
            }
        }

        // SingingAccounts must be in sorted order by AccountID.
        AccountID lastSigningAcctID = zero;

        for (auto const& signingAcct : signingAccounts)
        {
            auto const signingAcctID =
                signingAcct.getAccountID (sfAccount);

            // None of the multi-signers may sign for themselves.
            if (signingForID == signingAcctID)
                return false;

            // Accounts must be in order by account ID.  No duplicates allowed.
            if (lastSigningAcctID >= signingAcctID)
                return false;

            // The next signature must be greater than this one.
            lastSigningAcctID = signingAcctID;

            // If signingForID *is* txnAccountID, then look for duplicates.
            if (directSigning)
            {
                if (! firstLevelSigners.insert (signingAcctID).second)
                    // This is a duplicate signer.  Fail.
                    return false;
            }

            // Verify the signature.
            bool validSig = false;
            try
            {
                Serializer s = dataStart;
                finishMultiSigningData (signingForID, signingAcctID, s);

                RippleAddress const pubKey =
                    RippleAddress::createAccountPublic (
                        signingAcct.getFieldVL (sfSigningPubKey));

                Blob const signature =
                    signingAcct.getFieldVL (sfMultiSignature);

                validSig = pubKey.accountPublicVerify (
                    s.getData(), signature, fullyCanonical);
            }
            catch (...)
            {
                // We assume any problem lies with the signature.
                validSig = false;
            }
            if (!validSig)
                return false;
        }
    }

    // All signatures verified.
    return true;
}

//------------------------------------------------------------------------------

static
bool
isMemoOkay (STObject const& st, std::string& reason)
{
    if (!st.isFieldPresent (sfMemos))
        return true;

    auto const& memos = st.getFieldArray (sfMemos);

    // The number 2048 is a preallocation hint, not a hard limit
    // to avoid allocate/copy/free's
    Serializer s (2048);
    memos.add (s);

    // FIXME move the memo limit into a config tunable
    if (s.getDataLength () > 1024)
    {
        reason = "The memo exceeds the maximum allowed size.";
        return false;
    }

    for (auto const& memo : memos)
    {
        auto memoObj = dynamic_cast <STObject const*> (&memo);

        if (!memoObj || (memoObj->getFName() != sfMemo))
        {
            reason = "A memo array may contain only Memo objects.";
            return false;
        }

        for (auto const& memoElement : *memoObj)
        {
            auto const& name = memoElement.getFName();

            if (name != sfMemoType &&
                name != sfMemoData &&
                name != sfMemoFormat)
            {
                reason = "A memo may contain only MemoType, MemoData or "
                         "MemoFormat fields.";
                return false;
            }

            // The raw data is stored as hex-octets, which we want to decode.
            auto data = strUnHex (memoElement.getText ());

            if (!data.second)
            {
                reason = "The MemoType, MemoData and MemoFormat fields may "
                         "only contain hex-encoded data.";
                return false;
            }

            if (name == sfMemoData)
                continue;

            // The only allowed characters for MemoType and MemoFormat are the
            // characters allowed in URLs per RFC 3986: alphanumerics and the
            // following symbols: -._~:/?#[]@!$&'()*+,;=%
            static std::array<char, 256> const allowedSymbols = []
            {
                std::array<char, 256> a;
                a.fill(0);

                std::string symbols (
                    "0123456789"
                    "-._~:/?#[]@!$&'()*+,;=%"
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                    "abcdefghijklmnopqrstuvwxyz");

                for(char c : symbols)
                    a[c] = 1;
                return a;
            }();

            for (auto c : data.first)
            {
                if (!allowedSymbols[c])
                {
                    reason = "The MemoType and MemoFormat fields may only "
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
static
bool
isAccountFieldOkay (STObject const& st)
{
    for (int i = 0; i < st.getCount(); ++i)
    {
        auto t = dynamic_cast<STAccount const*>(st.peekAtPIndex (i));
        if (t && !t->isValueH160 ())
            return false;
    }

    return true;
}

bool passesLocalChecks (STObject const& st, std::string& reason)
{
    if (!isMemoOkay (st, reason))
        return false;

    if (!isAccountFieldOkay (st))
    {
        reason = "An account field is invalid.";
        return false;
    }

    return true;
}

} // ripple
