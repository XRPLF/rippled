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

#ifndef RIPPLE_SERIALIZEDTRANSACTION_H
#define RIPPLE_SERIALIZEDTRANSACTION_H

namespace ripple {

// VFALCO TODO replace these macros with language constants

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

    typedef std::shared_ptr<SerializedTransaction>        pointer;
    typedef const std::shared_ptr<SerializedTransaction>& ref;

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

    std::uint32_t getSequence () const
    {
        return getFieldU32 (sfSequence);
    }
    void setSequence (std::uint32_t seq)
    {
        return setFieldU32 (sfSequence, seq);
    }

    std::vector<RippleAddress> getMentionedAccounts () const;

    uint256 getTransactionID () const;

    virtual Json::Value getJson (int options) const;
    virtual Json::Value getJson (int options, bool binary) const;

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
    std::string getSQL (std::string & sql, std::uint32_t inLedger, char status) const;
    std::string getSQL (std::uint32_t inLedger, char status) const;
    std::string getSQL (Serializer rawTxn, std::uint32_t inLedger, char status) const;

    // SQL Functions with metadata
    static std::string getMetaSQLValueHeader ();
    static std::string getMetaSQLInsertReplaceHeader ();
    std::string getMetaSQL (std::uint32_t inLedger, const std::string & escapedMetaData) const;
    std::string getMetaSQL (Serializer rawTxn, std::uint32_t inLedger, char status, const std::string & escapedMetaData) const;

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

bool passesLocalChecks (STObject const& st, std::string&);
bool passesLocalChecks (STObject const& st);

} // ripple

#endif
