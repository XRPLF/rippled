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

#ifndef RIPPLE_TRANSACTIONMETA_H
#define RIPPLE_TRANSACTIONMETA_H

class TransactionMetaSet : LeakChecked <TransactionMetaSet>
{
public:
    typedef boost::shared_ptr<TransactionMetaSet> pointer;
    typedef const pointer& ref;

public:
    TransactionMetaSet () : mLedger (0), mIndex (static_cast<uint32> (-1)), mResult (255)
    {
        ;
    }
    TransactionMetaSet (uint256 const& txID, uint32 ledger, uint32 index) :
        mTransactionID (txID), mLedger (ledger), mIndex (static_cast<uint32> (-1)), mResult (255)
    {
        ;
    }
    TransactionMetaSet (uint256 const& txID, uint32 ledger, Blob const&);

    void init (uint256 const& transactionID, uint32 ledger);
    void clear ()
    {
        mNodes.clear ();
    }
    void swap (TransactionMetaSet&);

    uint256 const& getTxID ()
    {
        return mTransactionID;
    }
    uint32 getLgrSeq ()
    {
        return mLedger;
    }
    int getResult () const
    {
        return mResult;
    }
    TER getResultTER () const
    {
        return static_cast<TER> (mResult);
    }
    uint32 getIndex () const
    {
        return mIndex;
    }

    bool isNodeAffected (uint256 const& ) const;
    void setAffectedNode (uint256 const& , SField::ref type, uint16 nodeType);
    STObject& getAffectedNode (SLE::ref node, SField::ref type); // create if needed
    STObject& getAffectedNode (uint256 const& );
    const STObject& peekAffectedNode (uint256 const& ) const;
    std::vector<RippleAddress> getAffectedAccounts ();


    Json::Value getJson (int p) const
    {
        return getAsObject ().getJson (p);
    }
    void addRaw (Serializer&, TER, uint32 index);

    STObject getAsObject () const;
    STArray& getNodes ()
    {
        return (mNodes);
    }

    void setDeliveredAmount (STAmount const& delivered)
    {
        mDelivered.reset (delivered);
    }

    STAmount getDeliveredAmount () const
    {
        assert (hasDeliveredAmount ());
        return *mDelivered;
    }

    bool hasDeliveredAmount () const
    {
         return mDelivered;
    }

    static bool thread (STObject& node, uint256 const& prevTxID, uint32 prevLgrID);

private:
    uint256 mTransactionID;
    uint32  mLedger;
    uint32  mIndex;
    int     mResult;

    boost::optional <STAmount> mDelivered;

    STArray mNodes;
};

#endif

// vim:ts=4
