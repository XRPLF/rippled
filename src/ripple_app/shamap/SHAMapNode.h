//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_SHAMAPNODE_H
#define RIPPLE_SHAMAPNODE_H

// Identifies a node in a SHA256 hash map
class SHAMapNode
{
public:
    SHAMapNode () : mDepth (0), mHash (0)
    {
        ;
    }
    SHAMapNode (int depth, uint256 const& hash);

    int getDepth () const
    {
        return mDepth;
    }
    uint256 const& getNodeID ()  const
    {
        return mNodeID;
    }
    bool isValid () const
    {
        return (mDepth >= 0) && (mDepth < 64);
    }
    bool isRoot () const
    {
        return mDepth == 0;
    }
    size_t getMHash () const
    {
        if (mHash == 0) setMHash ();

        return mHash;
    }

    virtual bool isPopulated () const
    {
        return false;
    }

    SHAMapNode getParentNodeID () const
    {
        assert (mDepth);
        return SHAMapNode (mDepth - 1, mNodeID);
    }

    SHAMapNode getChildNodeID (int m) const;
    int selectBranch (uint256 const& hash) const;

    bool operator< (const SHAMapNode&) const;
    bool operator> (const SHAMapNode&) const;
    bool operator<= (const SHAMapNode&) const;
    bool operator>= (const SHAMapNode&) const;

    bool operator== (const SHAMapNode& n) const
    {
        return (mDepth == n.mDepth) && (mNodeID == n.mNodeID);
    }
    bool operator== (uint256 const& n) const
    {
        return n == mNodeID;
    }
    bool operator!= (const SHAMapNode& n) const
    {
        return (mDepth != n.mDepth) || (mNodeID != n.mNodeID);
    }
    bool operator!= (uint256 const& n) const
    {
        return n != mNodeID;
    }

    virtual std::string getString () const;
    void dump () const;

    static bool ClassInit ();
    static uint256 getNodeID (int depth, uint256 const& hash);

    // Convert to/from wire format (256-bit nodeID, 1-byte depth)
    void addIDRaw (Serializer& s) const;
    std::string getRawString () const;
    static int getRawIDLength (void)
    {
        return 33;
    }
    SHAMapNode (const void* ptr, int len);

protected:
    SHAMapNode (int depth, uint256 const& id, bool) : mNodeID (id), mDepth (depth), mHash (0)
    {
        ;
    }

private:
    static uint256 smMasks[65]; // AND with hash to get node id

    uint256 mNodeID;
    int     mDepth;
    mutable size_t  mHash;

    void setMHash () const;
};

extern std::size_t hash_value (const SHAMapNode& mn);

inline std::ostream& operator<< (std::ostream& out, const SHAMapNode& node)
{
    return out << node.getString ();
}

#endif
