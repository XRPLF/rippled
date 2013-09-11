//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SERIALIZEDTRANSACTION_H
#define RIPPLE_SERIALIZEDTRANSACTION_H

// VFALCO TODO eliminate these macros

#define TXN_SQL_NEW         'N'
#define TXN_SQL_CONFLICT    'C'
#define TXN_SQL_HELD        'H'
#define TXN_SQL_VALIDATED   'V'
#define TXN_SQL_INCLUDED    'I'
#define TXN_SQL_UNKNOWN     'U'

class SerializedTransaction
    : public STObject
    , public CountedObject <SerializedTransaction>
{
public:
    static char const* getCountedObjectName () { return "SerializedTransaction"; }

    typedef boost::shared_ptr<SerializedTransaction>        pointer;
    typedef const boost::shared_ptr<SerializedTransaction>& ref;

public:
    SerializedTransaction (SerializerIterator & sit);
    SerializedTransaction (TxType type);
    SerializedTransaction (const STObject & object);

    // STObject functions
    SerializedTypeID getSType () const
    {
        return STI_TRANSACTION;
    }
    std::string getFullText () const;
    std::string getText () const;

    // outer transaction functions / signature functions
    Blob getSignature () const;
    void setSignature (Blob const & s)
    {
        setFieldVL (sfTxnSignature, s);
    }
    uint256 getSigningHash () const;

    TxType getTxnType () const
    {
        return mType;
    }
    STAmount getTransactionFee () const
    {
        return getFieldAmount (sfFee);
    }
    void setTransactionFee (const STAmount & fee)
    {
        setFieldAmount (sfFee, fee);
    }

    RippleAddress getSourceAccount () const
    {
        return getFieldAccount (sfAccount);
    }
    Blob getSigningPubKey () const
    {
        return getFieldVL (sfSigningPubKey);
    }
    void setSigningPubKey (const RippleAddress & naSignPubKey);
    void setSourceAccount (const RippleAddress & naSource);
    std::string getTransactionType () const
    {
        return mFormat->getName ();
    }

    uint32 getSequence () const
    {
        return getFieldU32 (sfSequence);
    }
    void setSequence (uint32 seq)
    {
        return setFieldU32 (sfSequence, seq);
    }

    std::vector<RippleAddress> getMentionedAccounts () const;

    uint256 getTransactionID () const;

    virtual Json::Value getJson (int options, bool binary = false) const;

    void sign (const RippleAddress & naAccountPrivate);
    bool checkSign (const RippleAddress & naAccountPublic) const;
    bool checkSign () const;
    bool isKnownGood () const
    {
        return mSigGood;
    }
    bool isKnownBad () const
    {
        return mSigBad;
    }
    void setGood () const
    {
        mSigGood = true;
    }
    void setBad () const
    {
        mSigBad = true;
    }

    // SQL Functions
    static std::string getSQLValueHeader ();
    static std::string getSQLInsertHeader ();
    static std::string getSQLInsertIgnoreHeader ();
    std::string getSQL (std::string & sql, uint32 inLedger, char status) const;
    std::string getSQL (uint32 inLedger, char status) const;
    std::string getSQL (Serializer rawTxn, uint32 inLedger, char status) const;

    // SQL Functions with metadata
    static std::string getMetaSQLValueHeader ();
    static std::string getMetaSQLInsertReplaceHeader ();
    std::string getMetaSQL (uint32 inLedger, const std::string & escapedMetaData) const;
    std::string getMetaSQL (Serializer rawTxn, uint32 inLedger, char status, const std::string & escapedMetaData) const;

private:
    TxType mType;
    TxFormats::Item const* mFormat;

    SerializedTransaction* duplicate () const
    {
        return new SerializedTransaction (*this);
    }

    mutable bool mSigGood;
    mutable bool mSigBad;
};

#endif
// vim:ts=4
