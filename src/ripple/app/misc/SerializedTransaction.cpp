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

#include <ripple/core/Config.h>
#include <beast/unit_test/suite.h>
#include <boost/foreach.hpp>

namespace ripple {

SerializedTransaction::SerializedTransaction (TxType type)
    : STObject (sfTransaction)
    , tx_type_ (type)
    , sig_state_ (boost::indeterminate)
{
    auto format = TxFormats::getInstance().findByType (type);

    if (format == nullptr)
    {
        WriteLog (lsWARNING, SerializedTransaction) <<
            "Transaction type: " << type;
        throw std::runtime_error ("invalid transaction type");
    }

    set (format->elements);
    setFieldU16 (sfTransactionType, format->getType ());
}

SerializedTransaction::SerializedTransaction (STObject const& object)
    : STObject (object)
    , sig_state_ (boost::indeterminate)
{
    tx_type_ = static_cast <TxType> (getFieldU16 (sfTransactionType));

    auto format = TxFormats::getInstance().findByType (tx_type_);

    if (!format)
    {
        WriteLog (lsWARNING, SerializedTransaction) <<
            "Transaction type: " << tx_type_;
        throw std::runtime_error ("invalid transaction type");
    }

    if (!setType (format->elements))
    {
        WriteLog (lsWARNING, SerializedTransaction) <<
            "Transaction not legal for format";
        throw std::runtime_error ("transaction not valid");
    }
}

SerializedTransaction::SerializedTransaction (SerializerIterator& sit)
    : STObject (sfTransaction)
    , sig_state_ (boost::indeterminate)
{
    int length = sit.getBytesLeft ();

    if ((length < Protocol::txMinSizeBytes) || (length > Protocol::txMaxSizeBytes))
    {
        WriteLog (lsERROR, SerializedTransaction) <<
            "Transaction has invalid length: " << length;
        throw std::runtime_error ("Transaction length invalid");
    }

    set (sit);
    tx_type_ = static_cast<TxType> (getFieldU16 (sfTransactionType));

    auto format = TxFormats::getInstance().findByType (tx_type_);

    if (!format)
    {
        WriteLog (lsWARNING, SerializedTransaction) <<
            "Invalid transaction type: " << tx_type_;
        throw std::runtime_error ("invalid transaction type");
    }

    if (!setType (format->elements))
    {
        WriteLog (lsWARNING, SerializedTransaction) <<
            "Transaction not legal for format";
        throw std::runtime_error ("transaction not valid");
    }
}

std::string
SerializedTransaction::getFullText () const
{
    std::string ret = "\"";
    ret += to_string (getTransactionID ());
    ret += "\" = {";
    ret += STObject::getFullText ();
    ret += "}";
    return ret;
}

std::vector<RippleAddress>
SerializedTransaction::getMentionedAccounts () const
{
    std::vector<RippleAddress> accounts;

    for (auto const& it : peekData ())
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

uint256
SerializedTransaction::getSigningHash () const
{
    return STObject::getSigningHash (HashPrefix::txSign);
}

uint256
SerializedTransaction::getTransactionID () const
{
    return getHash (HashPrefix::transactionID);
}

Blob SerializedTransaction::getSignature () const
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

void SerializedTransaction::sign (RippleAddress const& private_key)
{
    Blob signature;
    private_key.accountPrivateSign (getSigningHash (), signature);
    setFieldVL (sfTxnSignature, signature);
}

bool SerializedTransaction::checkSign () const
{
    if (boost::indeterminate (sig_state_))
    {
        try
        {
            RippleAddress n;
            n.setAccountPublic (getFieldVL (sfSigningPubKey));

            sig_state_ = checkSign (n);
        }
        catch (...)
        {
            sig_state_ = false;
        }
    }

    assert (!boost::indeterminate (sig_state_));

    return static_cast<bool> (sig_state_);
}

bool SerializedTransaction::checkSign (RippleAddress const& public_key) const
{
    try
    {
        ECDSA const fullyCanonical = (getFlags() & tfFullyCanonicalSig)
            ? ECDSA::strict
            : ECDSA::not_strict;

        return public_key.accountPublicVerify (getSigningHash (),
            getFieldVL (sfTxnSignature), fullyCanonical);
    }
    catch (...)
    {
        return false;
    }
}

void SerializedTransaction::setSigningPubKey (RippleAddress const& naSignPubKey)
{
    setFieldVL (sfSigningPubKey, naSignPubKey.getAccountPublic ());
}

void SerializedTransaction::setSourceAccount (RippleAddress const& naSource)
{
    setFieldAccount (sfAccount, naSource);
}

Json::Value SerializedTransaction::getJson (int) const
{
    Json::Value ret = STObject::getJson (0);
    ret["hash"] = to_string (getTransactionID ());
    return ret;
}

Json::Value SerializedTransaction::getJson (int options, bool binary) const
{
    if (binary)
    {
        Json::Value ret;
        Serializer s = STObject::getSerializer ();
        ret["tx"] = strHex (s.peekData ());
        ret["hash"] = to_string (getTransactionID ());
        return ret;
    }
    return getJson(options);
}

std::string const&
SerializedTransaction::getMetaSQLInsertReplaceHeader ()
{
    static std::string const sql = "INSERT OR REPLACE INTO Transactions "
        "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, TxnMeta)"
        " VALUES ";

    return sql;
}

std::string SerializedTransaction::getMetaSQL (std::uint32_t inLedger,
                                               std::string const& escapedMetaData) const
{
    Serializer s;
    add (s);
    return getMetaSQL (s, inLedger, TXN_SQL_VALIDATED, escapedMetaData);
}

std::string
SerializedTransaction::getMetaSQL (Serializer rawTxn,
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

bool isMemoOkay (STObject const& st)
{
    if (!st.isFieldPresent (sfMemos))
        return true;

    const STArray& memos = st.getFieldArray (sfMemos);
    // The number 2048 is a preallocation hint, not a hard limit
    // to avoid allocate/copy/free's
    Serializer s (2048);
    memos.add (s);
    // FIXME move the memo limit into a config tunable
    if (s.getDataLength () > 1024)
        return false;

    for (auto const& memo : memos)
    {
        auto memoObj = dynamic_cast <STObject const*> (&memo);

        // Memos array must consist solely of Memo objects
        if (!memoObj || (memoObj->getFName() != sfMemo))
        {
            return false;
        }

        // Memo objects may only contain
        // MemoType, MemoData, and MemoFormat fields
        for (auto const& memoElement : *memoObj)
        {
            if ((memoElement.getFName() != sfMemoType) &&
                (memoElement.getFName() != sfMemoData) &&
                (memoElement.getFName() != sfMemoFormat))
            {
                return false;
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
    if (!isMemoOkay (st))
    {
        reason = "The memo exceeds the maximum allowed size.";
        return false;
    }
    if (!isAccountFieldOkay (st))
    {
        reason = "An account field is invalid.";
        return false;
    }

    return true;
}

bool passesLocalChecks (STObject const& st)
{
    std::string reason;
    return passesLocalChecks (st, reason);
}


//------------------------------------------------------------------------------

class SerializedTransaction_test : public beast::unit_test::suite
{
public:
    void run()
    {
        RippleAddress seed;
        seed.setSeedRandom ();
        RippleAddress generator = RippleAddress::createGeneratorPublic (seed);
        RippleAddress publicAcct = RippleAddress::createAccountPublic (generator, 1);
        RippleAddress privateAcct = RippleAddress::createAccountPrivate (generator, seed, 1);

        SerializedTransaction j (ttACCOUNT_SET);
        j.setSourceAccount (publicAcct);
        j.setSigningPubKey (publicAcct);
        j.setFieldVL (sfMessageKey, publicAcct.getAccountPublic ());
        j.sign (privateAcct);

        unexpected (!j.checkSign (), "Transaction fails signature test");

        Serializer rawTxn;
        j.add (rawTxn);
        SerializerIterator sit (rawTxn);
        SerializedTransaction copy (sit);

        if (copy != j)
        {
            log << j.getJson (0);
            log << copy.getJson (0);
            fail ("Transaction fails serialize/deserialize test");
        }
        else
        {
            pass ();
        }

        STParsedJSON parsed ("test", j.getJson (0));
        std::unique_ptr <STObject> new_obj (std::move (parsed.object));

        if (new_obj.get () == nullptr)
            fail ("Unable to build object from json");

        if (STObject (j) != *new_obj)
        {
            log << "ORIG: " << j.getJson (0);
            log << "BUILT " << new_obj->getJson (0);
            fail ("Built a different transaction");
        }
        else
        {
            pass ();
        }
    }
};

BEAST_DEFINE_TESTSUITE(SerializedTransaction,ripple_app,ripple);

} // ripple
