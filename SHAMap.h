#ifndef __SHAMAP__
#define __SHAMAP__

#include "uint256.h"

class SHAMapNodeID
{
public:
	int	mDepth;
	uint256	mNodeID;
	
        bool operator<(const Transaction &) const;
        bool operator>(const Transaction &) const;
        bool operator==(const Transaction &) const;
        bool operator!=(const Transaction &) const;
        bool operator<=(const Transaction &) const;
        bool operator>=(const Transaction &) const;
};

class SHAMapLeafNode
{
private:
	uint256			mNodeID;
	std::list<uint256>	mHashes;
	uint256			mNodeHash;

public:
	SHAMapLeafNode(const uint256& NodeID);

	const uint256& GetNodeID(void) const { return mNodeID; }
	bool HasHash(const uint256& Hash) const;

	uint256& GetNodeHash();

	bool AddHash(const uint256& Hash);
	bool DelHash(const uint256& Hash);
};

typedef boost::shared_ptr<SHAMapLeafNode> pointer;

class SHAMapInnerNode
{
private:
	int		mDepth;
	uint256		mNodeID;
	uint256		mHash;
	uint256[32]	mHashes;

public:
	SHAMapInnerNode(int Depth, const uint256 &NodeID);

	int GetDepth() const { return mDepth; }
	const uint256& GetNodeID() const { return mNodeID; }
	const uint256& GetChildHash(int m) const;
	const uint256& GetNodeHash() const;

	bool IsRootNode() const { return mDepth==0; }
	bool IsLastInnerNode() const { return mDepth==9; }

	uint256 GetChildNodeID(int m) const;
	void SetChildHash(int m, const uint256& Hash);
	void SetChildHash(const SHAMapInnerNode &mChild);
	void SetChildHash(const SHAMapLeafNode &mLeaf);
};

typedef boost::shared_ptr<SHAMapInnerNode> pointer;

// Not all nodes need to be resident in memory, the class can hold subsets
// This class does not handle any inter-node logic because that requires reads/writes

class SHAMap
{
private:
	boost::mutex mLock;
	std::map<SHAMapNodeID, SHAMapLeafNode> mLeaves
	std::multimap<SHAMapNodeID, SHAMapInnerNode> mInnerNodes;

public:
	SHAMap();

	bool HasInnerNode(int m, const uint160 &nodeID);
	bool GiveInnerNode(SHAMapInnerNode::pointer);
	SHAMapInnerNode::pointer GetInnerNode(int m, const uint160 &nodeID);

	bool HasLeafNode(int m, const uint160 &nodeID);
	bool GiveLeafNode(SHAMapLeafNode::pointer);
	SHAMapLeafNode::pointer GetLeafNode(int m, const uint160 &nodeID);
};
