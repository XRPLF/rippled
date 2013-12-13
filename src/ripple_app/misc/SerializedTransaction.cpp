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

SETUP_LOG (SerializedTransaction)

SerializedTransaction::SerializedTransaction (TxType type)
    : STObject (sfTransaction)
    , mType (type)
    , mSigGood (false)
    , mSigBad (false)
{
    mFormat = TxFormats::getInstance()->findByType (type);

    if (mFormat == nullptr)
    {
        WriteLog (lsWARNING, SerializedTransaction) << "Transaction type: " << type;
        throw std::runtime_error ("invalid transaction type");
    }

    set (mFormat->elements);
    setFieldU16 (sfTransactionType, mFormat->getType ());
}

SerializedTransaction::SerializedTransaction (STObject const& object)
    : STObject (object)
    , mSigGood (false)
    , mSigBad (false)
{
    mType = static_cast <TxType> (getFieldU16 (sfTransactionType));

    mFormat = TxFormats::getInstance()->findByType (mType);

    if (!mFormat)
    {
        WriteLog (lsWARNING, SerializedTransaction) << "Transaction type: " << mType;
        throw std::runtime_error ("invalid transaction type");
    }

    if (!setType (mFormat->elements))
    {
        throw std::runtime_error ("transaction not valid");
    }
}

SerializedTransaction::SerializedTransaction (SerializerIterator& sit) : STObject (sfTransaction),
    mSigGood (false), mSigBad (false)
{
    int length = sit.getBytesLeft ();

    if ((length < Protocol::txMinSizeBytes) || (length > Protocol::txMaxSizeBytes))
    {
        Log (lsERROR) << "Transaction has invalid length: " << length;
        throw std::runtime_error ("Transaction length invalid");
    }

    set (sit);
    mType = static_cast<TxType> (getFieldU16 (sfTransactionType));

    mFormat = TxFormats::getInstance()->findByType (mType);

    if (!mFormat)
    {
        WriteLog (lsWARNING, SerializedTransaction) << "Transaction type: " << mType;
        throw std::runtime_error ("invalid transaction type");
    }

    if (!setType (mFormat->elements))
    {
        WriteLog (lsWARNING, SerializedTransaction) << "Transaction not legal for format";
        throw std::runtime_error ("transaction not valid");
    }
}

std::string SerializedTransaction::getFullText () const
{
    std::string ret = "\"";
    ret += getTransactionID ().GetHex ();
    ret += "\" = {";
    ret += STObject::getFullText ();
    ret += "}";
    return ret;
}

std::string SerializedTransaction::getText () const
{
    return STObject::getText ();
}

std::vector<RippleAddress> SerializedTransaction::getMentionedAccounts () const
{
    std::vector<RippleAddress> accounts;

    BOOST_FOREACH (const SerializedType & it, peekData ())
    {
        const STAccount* sa = dynamic_cast<const STAccount*> (&it);

        if (sa != NULL)
        {
            bool found = false;
            RippleAddress na = sa->getValueNCA ();
            BOOST_FOREACH (const RippleAddress & it, accounts)
            {
                if (it == na)
                {
                    found = true;
                    break;
                }
            }

            if (!found)
                accounts.push_back (na);
        }

        const STAmount* sam = dynamic_cast<const STAmount*> (&it);

        if (sam)
        {
            uint160 issuer = sam->getIssuer ();

            if (issuer.isNonZero ())
            {
                RippleAddress na;
                na.setAccountID (issuer);
                bool found = false;
                BOOST_FOREACH (const RippleAddress & it, accounts)
                {
                    if (it == na)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                    accounts.push_back (na);
            }
        }
    }
    return accounts;
}

uint256 SerializedTransaction::getSigningHash () const
{
    return STObject::getSigningHash (getConfig ().SIGN_TRANSACTION);
}

uint256 SerializedTransaction::getTransactionID () const
{
    // perhaps we should cache this
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

void SerializedTransaction::sign (const RippleAddress& naAccountPrivate)
{
    Blob signature;
    naAccountPrivate.accountPrivateSign (getSigningHash (), signature);
    setFieldVL (sfTxnSignature, signature);
}

bool SerializedTransaction::checkSign () const
{
    if (mSigGood)
        return true;

    if (mSigBad)
        return false;

    try
    {
        RippleAddress n;
        n.setAccountPublic (getFieldVL (sfSigningPubKey));

        if (checkSign (n))
        {
            mSigGood = true;
            return true;
        }
    }
    catch (...)
    {
        ;
    }

    mSigBad = true;
    return false;
}

bool SerializedTransaction::checkSign (const RippleAddress& naAccountPublic) const
{
    try
    {
        return naAccountPublic.accountPublicVerify (getSigningHash (), getFieldVL (sfTxnSignature));
    }
    catch (...)
    {
        return false;
    }
}

void SerializedTransaction::setSigningPubKey (const RippleAddress& naSignPubKey)
{
    setFieldVL (sfSigningPubKey, naSignPubKey.getAccountPublic ());
}

void SerializedTransaction::setSourceAccount (const RippleAddress& naSource)
{
    setFieldAccount (sfAccount, naSource);
}

Json::Value SerializedTransaction::getJson (int options, bool binary) const
{
    if (binary)
    {
        Json::Value ret;
        Serializer s = STObject::getSerializer ();
        ret["tx"] = strHex (s.peekData ());
        ret["hash"] = getTransactionID ().GetHex ();
        return ret;
    }

    Json::Value ret = STObject::getJson (0);
    ret["hash"] = getTransactionID ().GetHex ();
    return ret;
}

std::string SerializedTransaction::getSQLValueHeader ()
{
    return "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn)";
}

std::string SerializedTransaction::getMetaSQLValueHeader ()
{
    return "(TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, TxnMeta)";
}

std::string SerializedTransaction::getMetaSQLInsertReplaceHeader ()
{
    return "INSERT OR REPLACE INTO Transactions " + getMetaSQLValueHeader () + " VALUES ";
}

std::string SerializedTransaction::getSQL (uint32 inLedger, char status) const
{
    Serializer s;
    add (s);
    return getSQL (s, inLedger, status);
}

std::string SerializedTransaction::getMetaSQL (uint32 inLedger, const std::string& escapedMetaData) const
{
    Serializer s;
    add (s);
    return getMetaSQL (s, inLedger, TXN_SQL_VALIDATED, escapedMetaData);
}

std::string SerializedTransaction::getSQL (Serializer rawTxn, uint32 inLedger, char status) const
{
    static boost::format bfTrans ("('%s', '%s', '%s', '%d', '%d', '%c', %s)");
    std::string rTxn    = sqlEscape (rawTxn.peekData ());

    return str (boost::format (bfTrans)
                % getTransactionID ().GetHex () % getTransactionType () % getSourceAccount ().humanAccountID ()
                % getSequence () % inLedger % status % rTxn);
}

std::string SerializedTransaction::getMetaSQL (Serializer rawTxn, uint32 inLedger, char status,
        const std::string& escapedMetaData) const
{
    static boost::format bfTrans ("('%s', '%s', '%s', '%d', '%d', '%c', %s, %s)");
    std::string rTxn    = sqlEscape (rawTxn.peekData ());

    return str (boost::format (bfTrans)
                % getTransactionID ().GetHex () % getTransactionType () % getSourceAccount ().humanAccountID ()
                % getSequence () % inLedger % status % rTxn % escapedMetaData);
}

//------------------------------------------------------------------------------

class SerializedTransactionTests : public UnitTest
{
public:
    SerializedTransactionTests () : UnitTest ("SerializedTransaction", "ripple")
    {
    }

    void runTest ()
    {
        beginTestCase ("tx");

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
            Log (lsFATAL) << j.getJson (0);
            Log (lsFATAL) << copy.getJson (0);
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
            Log (lsINFO) << "ORIG: " << j.getJson (0);
            Log (lsINFO) << "BUILT " << new_obj->getJson (0);
            fail ("Built a different transaction");
        }
        else
        {
            pass ();
        }
    }
};

static SerializedTransactionTests serializedTransactionTests;
