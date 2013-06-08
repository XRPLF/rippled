#ifndef RIPPLE_HASHROUTER_H
#define RIPPLE_HASHROUTER_H

DEFINE_INSTANCE (HashRouterEntry);

// VFALCO TODO convert these macros to int constants
#define SF_RELAYED		0x01	// Has already been relayed to other nodes
#define SF_BAD			0x02	// Signature/format is bad
#define SF_SIGGOOD		0x04	// Signature is good
#define SF_SAVED		0x08
#define SF_RETRY		0x10	// Transaction can be retried
#define SF_TRUSTED		0x20	// comes from trusted source

// VFALCO TODO move this class into the scope of class HashRouter
class HashRouterEntry : private IS_INSTANCE (HashRouterEntry)
{
public:
	HashRouterEntry ()	: mFlags(0)					{ ; }

	const std::set<uint64>& peekPeers()			{ return mPeers; }
	void addPeer(uint64 peer)					{ if (peer != 0) mPeers.insert(peer); }
	bool hasPeer(uint64 peer)					{ return mPeers.count(peer) > 0; }

	int getFlags(void)							{ return mFlags; }
	bool hasFlag(int f)							{ return (mFlags & f) != 0; }
	void setFlag(int f)							{ mFlags |= f; }
	void clearFlag(int f)						{ mFlags &= ~f; }
	void swapSet(std::set<uint64>& s)			{ mPeers.swap(s); }

protected:
	int						mFlags;
	std::set<uint64>		mPeers;
};

class IHashRouter
{
public:
    // VFALCO NOTE this preferred alternative to default parameters makes
    //         behavior clear.
    //
    static inline int getDefaultHoldTime ()
    {
        return 120;
    }

    // VFALCO TODO rename the parameter to entryHoldTimeInSeconds
    static IHashRouter* New (int holdTime);

    virtual ~IHashRouter () { }

	virtual bool addSuppression(uint256 const& index) = 0;

	virtual bool addSuppressionPeer(uint256 const& index, uint64 peer) = 0;
	virtual bool addSuppressionPeer(uint256 const& index, uint64 peer, int& flags) = 0;
	virtual bool addSuppressionFlags(uint256 const& index, int flag) = 0;
	virtual bool setFlag(uint256 const& index, int flag) = 0;
	virtual int getFlags(uint256 const& index) = 0;

	virtual HashRouterEntry getEntry(uint256 const& ) = 0;

	virtual bool swapSet(uint256 const& index, std::set<uint64>& peers, int flag) = 0;
};


#endif
