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

#include <ripple/protocol/STObject.h>
#include <ripple/protocol/TxFormats.h>
#include <boost/logic/tribool.hpp>

namespace ripple {

// VFALCO TODO replace these macros with language constants
#define TXN_SQL_NEW         'N'
#define TXN_SQL_CONFLICT    'C'
#define TXN_SQL_HELD        'H'
#define TXN_SQL_VALIDATED   'V'
#define TXN_SQL_INCLUDED    'I'
#define TXN_SQL_UNKNOWN     'U'

class STTx
    : public STObject
    , public CountedObject <STTx>
{
public:
    static char const* getCountedObjectName () { return "STTx"; }

    typedef std::shared_ptr<STTx>        pointer;
    typedef const std::shared_ptr<STTx>& ref;

public:
    STTx () = delete;
    STTx& operator= (STTx const& other) = delete;

    STTx (STTx const& other) = default;

    explicit STTx (SerializerIterator& sit);
    explicit STTx (TxType type);

    // Only called from ripple::RPC::transactionSign - can we eliminate this?
    explicit STTx (STObject const& object);

    // STObject functions
    SerializedTypeID getSType () const
    {
        return STI_TRANSACTION;
    }
    std::string getFullText () const;

    // outer transaction functions / signature functions
    Blob getSignature () const;

    uint256 getSigningHash () const;

    TxType getTxnType () const
    {
        return tx_type_;
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

    void sign (RippleAddress const& private_key);

    bool checkSign () const;

    bool isKnownGood () const
    {
        return (sig_state_ == true);
    }
    bool isKnownBad () const
    {
        return (sig_state_ == false);
    }
    void setGood () const
    {
        sig_state_ = true;
    }
    void setBad () const
    {
        sig_state_ = false;
    }

    // SQL Functions with metadata
    static
    std::string const&
    getMetaSQLInsertReplaceHeader ();

    std::string getMetaSQL (
        std::uint32_t inLedger, std::string const& escapedMetaData) const;

    std::string getMetaSQL (
        Serializer rawTxn,
        std::uint32_t inLedger,
        char status,
        std::string const& escapedMetaData) const;

private:
    STTx* duplicate () const override
    {
        return new STTx (*this);
    }

    TxType tx_type_;

    mutable boost::tribool sig_state_;
};

bool passesLocalChecks (STObject const& st, std::string&);
bool passesLocalChecks (STObject const& st);

} // ripple

#endif
