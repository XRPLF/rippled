#ifndef RIPPLE_SHAMAPMISSINGNODE_H
#define RIPPLE_SHAMAPMISSINGNODE_H

enum SHAMapType
{
    smtTRANSACTION  = 1,    // A tree of transactions
    smtSTATE        = 2,    // A tree of state nodes
    smtFREE         = 3,    // A tree not part of a ledger
};

class SHAMapMissingNode : public std::runtime_error
{
public:
    SHAMapMissingNode (SHAMapType t,
                       SHAMapNode const& nodeID,
                       uint256 const& nodeHash)
        : std::runtime_error ("SHAMapMissingNode")
        , mType (t)
        , mNodeID (nodeID)
        , mNodeHash (nodeHash)
    {
    }

    SHAMapMissingNode (SHAMapType t,
                       SHAMapNode const& nodeID,
                       uint256 const& nodeHash,
                       uint256 const& targetIndex)
        : std::runtime_error (nodeID.getString ())
        , mType (t)
        , mNodeID (nodeID)
        , mNodeHash (nodeHash)
        , mTargetIndex (targetIndex)
    {
    }

    virtual ~SHAMapMissingNode () throw ()
    {
    }

    void setTargetNode (uint256 const& tn)
    {
        mTargetIndex = tn;
    }

    SHAMapType getMapType () const
    {
        return mType;
    }

    SHAMapNode const& getNodeID () const
    {
        return mNodeID;
    }

    uint256 const& getNodeHash () const
    {
        return mNodeHash;
    }

    uint256 const& getTargetIndex () const
    {
        return mTargetIndex;
    }

    bool hasTargetIndex () const
    {
        return !mTargetIndex.isZero ();
    }

private:
    SHAMapType mType;
    SHAMapNode mNodeID;
    uint256 mNodeHash;
    uint256 mTargetIndex;
};

extern std::ostream& operator<< (std::ostream&, SHAMapMissingNode const&);

#endif
