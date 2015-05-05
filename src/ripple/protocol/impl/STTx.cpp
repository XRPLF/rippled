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
#include <ripple/basics/Log.h>
#include <ripple/protocol/HashPrefix.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Protocol.h>
#include <ripple/protocol/STAccount.h>
#include <ripple/protocol/STArray.h>
#include <ripple/protocol/STTx.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/basics/StringUtilities.h>
#include <ripple/json/to_string.h>
#include <ripple/legacy/0.27/Emulate027.h>
#include <beast/unit_test/suite.h>
#include <boost/format.hpp>
#include <array>

namespace ripple {

STTx::STTx (TxType type)
    : STObject (sfTransaction)
    , tx_type_ (type)
    , sig_state_ (boost::indeterminate)
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
    , sig_state_ (boost::indeterminate)
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
    , sig_state_ (boost::indeterminate)
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

std::vector<RippleAddress>
STTx::getMentionedAccounts () const
{
    std::vector<RippleAddress> accounts;

    for (auto const& it : *this)
    {
        if (auto sa = dynamic_cast<STAccount const*> (&it))
        {
            auto const na = sa->getValueNCA ();

            if (std::find (accounts.cbegin (), accounts.cend (), na) == accounts.cend ())
                accounts.push_back (na);
        }
        else if (auto sa = dynamic_cast<STAmount const*> (&it))
        {
            auto const& issuer = sa->getIssuer ();

            if (isXRP (issuer))
                continue;

            RippleAddress na;
            na.setAccountID (issuer);

            if (std::find (accounts.cbegin (), accounts.cend (), na) == accounts.cend ())
                accounts.push_back (na);
        }
    }

    return accounts;
}

static Blob getSigningData (STTx const& that)
{
    Serializer s;
    s.add32 (HashPrefix::txSign);
    that.add (s, false);
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

bool STTx::checkSign () const
{
    if (boost::indeterminate (sig_state_))
    {
        try
        {
            ECDSA const fullyCanonical = (getFlags() & tfFullyCanonicalSig)
                ? ECDSA::strict
                : ECDSA::not_strict;

            RippleAddress n;
            n.setAccountPublic (getFieldVL (sfSigningPubKey));

            sig_state_ = n.accountPublicVerify (getSigningData (*this),
                getFieldVL (sfTxnSignature), fullyCanonical);
        }
        catch (...)
        {
            sig_state_ = false;
        }
    }

    assert (!boost::indeterminate (sig_state_));

    return static_cast<bool> (sig_state_);
}

void STTx::setSigningPubKey (RippleAddress const& naSignPubKey)
{
    setFieldVL (sfSigningPubKey, naSignPubKey.getAccountPublic ());
}

void STTx::setSourceAccount (RippleAddress const& naSource)
{
    setFieldAccount (sfAccount, naSource);
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
                % getSourceAccount ().humanAccountID ()
                % getSequence () % inLedger % status % rTxn % escapedMetaData);
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
