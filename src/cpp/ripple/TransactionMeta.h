#ifndef RIPPLE_TRANSACTIONMETA_H
#define RIPPLE_TRANSACTIONMETA_H

class TransactionMetaSet
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

    static bool thread (STObject& node, uint256 const& prevTxID, uint32 prevLgrID);

private:
    uint256 mTransactionID;
    uint32  mLedger;
    uint32  mIndex;
    int     mResult;

    STArray mNodes;
};

#endif

// vim:ts=4
