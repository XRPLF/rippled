//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_HASHROUTER_RIPPLEHEADER
#define RIPPLE_HASHROUTER_RIPPLEHEADER

// VFALCO NOTE Are these the flags?? Why aren't we using a packed struct?
// VFALCO TODO convert these macros to int constants
#define SF_RELAYED      0x01    // Has already been relayed to other nodes
// VFALCO NOTE How can both bad and good be set on a hash?
#define SF_BAD          0x02    // Signature/format is bad
#define SF_SIGGOOD      0x04    // Signature is good
#define SF_SAVED        0x08
#define SF_RETRY        0x10    // Transaction can be retried
#define SF_TRUSTED      0x20    // comes from trusted source

/** Routing table for objects identified by hash.

    This table keeps track of which hashes have been received by which peers.
    It is used to manage the routing and broadcasting of messages in the peer
    to peer overlay.
*/
class IHashRouter
{
public:
    // VFALCO NOTE this preferred alternative to default parameters makes
    //         behavior clear.
    //
    static inline int getDefaultHoldTime ()
    {
        return 300;
    }

    // VFALCO TODO rename the parameter to entryHoldTimeInSeconds
    static IHashRouter* New (int holdTime);

    virtual ~IHashRouter () { }

    // VFALCO TODO Replace "Supression" terminology with something more semantically meaningful.
    virtual bool addSuppression (uint256 const& index) = 0;

    virtual bool addSuppressionPeer (uint256 const& index, uint64 peer) = 0;

    virtual bool addSuppressionPeer (uint256 const& index, uint64 peer, int& flags) = 0;

    virtual bool addSuppressionFlags (uint256 const& index, int flag) = 0;

    /** Set the flags on a hash.

        @return `true` if the flags were changed.
    */
    // VFALCO TODO Rename to setFlags since it works with multiple flags.
    virtual bool setFlag (uint256 const& index, int mask) = 0;

    virtual int getFlags (uint256 const& index) = 0;

    virtual bool swapSet (uint256 const& index, std::set<uint64>& peers, int flag) = 0;

    // VFALCO TODO This appears to be unused!
    //
//    virtual Entry getEntry (uint256 const&) = 0;
};


#endif
