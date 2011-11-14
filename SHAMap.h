#ifndef __SHAMAP__
#define __SHAMAP__

#include <list>

#include <boost/shared_ptr.hpp>

#include "uint256.h"
#include "ScopedLock.h"

class SHAMap;

// A tree-like map of SHA256 hashes
// 10 levels, consisting of a root, 9 interior node levels, and leaves
// The trees are designed for rapid synchronization and compression of differences


class SHAMapNodeID
{ // Identified a node in a SHA256 hash
private:
	static uint256	smMasks[11]; // AND with hash to get node id

	int	mDepth;
	uint256	mNodeID;

public:

	// 0 is root, 10 is leaf
	static const int rootDepth=0;
	static const int leafDepth=10;

	SHAMapNodeID(int depth, const uint256 &hash);
	int getDepth() { return mDepth; }
	const uint256 &getNodeID();

	bool isRoot() const { return mDepth==0; }
	bool isLeaf() const { return mDepth==leafDepth; }
	bool isInner() const { return !isRoot() && !isLeaf(); }
	SHAMapNodeID getParentNodeID() { return SHAMapNodeID(mDepth-1, mNodeID); }
	SHAMapNodeID getChildNodeID(int m);

	int selectBranch(const uint256 &hash);

	static uint256 getNodeID(int depth, const uint256 &hash);
	
        bool operator<(const SHAMapNodeID &) const;
        bool operator>(const SHAMapNodeID &) const;
        bool operator==(const SHAMapNodeID &) const;
        bool operator!=(const SHAMapNodeID &) const;
        bool operator<=(const SHAMapNodeID &) const;
        bool operator>=(const SHAMapNodeID &) const;

        static void ClassInit(void);
};


class SHAMapLeafNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapLeafNode> pointer;

private:
	SHAMapNodeID mNodeID;
	uint256	mHash;
	std::list<uint256> mHashes;

	void updateHash(void);

protected:
	bool hasHash(const uint256 &hash) const;
	bool addHash(const uint256 &hash);
	bool delHash(const uint256 &hash);

public:
	SHAMapLeafNode(const uint256& NodeID);

	const SHAMapNodeID& GetNodeID(void) const { return mNodeID; }
	const uint256& GetNodeHash() const { return mHash; }

};

class SHAMapInnerNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapInnerNode> pointer;

private:
	SHAMapNodeID	mNodeID;
	uint256		mHash;
	uint256		mHashes[32];

	void updateHash(void);

protected:
	void SetChildHash(int m, const uint256 &hash);

public:
	SHAMapInnerNode(int Depth, const uint256 &NodeID);
	const SHAMapNodeID& GetNodeID() const { return mNodeID; }
	const uint256& GetNodeHash() const  { return mHash; }
	const uint256& GetChildHash(int m) const;
};



class SHAMap
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;

private:
	mutable boost::mutex mLock;
	std::map<SHAMapNodeID, SHAMapLeafNode> mLeaves;
	std::multimap<SHAMapNodeID, SHAMapInnerNode> mInnerNodes;

public:
	SHAMap();

	ScopedLock Lock() const { return ScopedLock(mLock); }

	// inner node access functions
	bool HasInnerNode(const SHAMapNodeID &id);
	bool GiveInnerNode(SHAMapInnerNode::pointer);
	SHAMapInnerNode::pointer GetInnerNode(const SHAMapNodeID &);

	// leaf node access functions
	bool HasLeafNode(const SHAMapNodeID &id);
	bool GiveLeafNode(SHAMapLeafNode::pointer);
	SHAMapLeafNode::pointer GetLeafNode(const SHAMapNodeID &);

	// generic node functions
	std::vector<unsigned char> getRawNode(const SHAMapNodeID &id);
	bool addRawNode(const SHAMapNodeID& nodeID, std::vector<unsigned char> rawNode);

	// normal hash access functions
	bool hasHash(const uint256 &hash);
	bool addHash(const uint256 &hash);
	bool delHash(const uint256 &hash);
	bool firstHash(uint256 &hash);
	bool lastHash(uint256 &hash);
	bool nextHash(uint256 &hash);
	bool prevHash(uint256 &hash);

	// special overloads for backed maps
	virtual bool fetchNode(const uint256 &hash, std::vector<unsigned char>& rawNode);
	virtual bool writeNode(const uint256 &hash, const std::vector<unsigned char>& rawNode);
};

#endif
