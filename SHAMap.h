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


class SHAMapNode
{ // Identified a node in a SHA256 hash
private:
	static uint256	smMasks[11]; // AND with hash to get node id

	int	mDepth;
	uint256	mNodeID;

public:

	// 0 is root, 10 is leaf
	static const int rootDepth=0;
	static const int leafDepth=10;

	SHAMapNode(int depth, const uint256 &hash);
	int getDepth() { return mDepth; }
	const uint256 &getNodeID();

	bool isRoot() const { return mDepth==0; }
	bool isLeaf() const { return mDepth==leafDepth; }
	bool isInner() const { return !isRoot() && !isLeaf(); }
	virtual bool IsPopulated(void) const { return false; }
	SHAMapNode getParentNodeID() { return SHAMapNode(mDepth-1, mNodeID); }
	SHAMapNode getChildNodeID(int m);

	int selectBranch(const uint256 &hash);

	static uint256 getNodeID(int depth, const uint256 &hash);
	
        bool operator<(const SHAMapNode &) const;
        bool operator>(const SHAMapNode &) const;
        bool operator==(const SHAMapNode &) const;
        bool operator!=(const SHAMapNode &) const;
        bool operator<=(const SHAMapNode &) const;
        bool operator>=(const SHAMapNode &) const;

        static void ClassInit();
};


class SHAMapLeafNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapLeafNode> pointer;

private:
	uint256	mHash;
	std::list<uint256> mHashes;

	void updateHash();

protected:
	bool addHash(const uint256 &hash);
	bool delHash(const uint256 &hash);

public:
	SHAMapLeafNode(const SHAMapNode& nodeID);

	virtual bool IsPopulated(void) const { return true; }

	const uint256& GetNodeHash() const { return mHash; }
	bool isEmpty() const { return mHashes.empty(); }
	int getHashCount() const { return mHashes.size(); }
	const uint256& GetHash(int m) const;
	bool hasHash(const uint256 &hash) const;

};

class SHAMapInnerNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapInnerNode> pointer;

private:
	uint256		mHash;
	uint256		mHashes[32];

	void updateHash();

protected:
	void SetChildHash(int m, const uint256 &hash);

public:
	SHAMapInnerNode(int Depth, const uint256 &NodeID);


	virtual bool IsPopulated(void) const { return true; }
	const uint256& GetNodeHash() const  { return mHash; }
	const uint256& GetChildHash(int m) const;
	bool isEmpty() const;
};



class SHAMap
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;

private:
	mutable boost::mutex mLock;
	std::map<SHAMapNode, SHAMapLeafNode> mLeafByID;
	std::map<SHAMapNode, SHAMapInnerNode> mInnerNodeByID;
	std::map<uint256, SHAMapNode> mNodeByHash; // includes nodes not present

public:
	SHAMap();

	ScopedLock Lock() const { return ScopedLock(mLock); }

	// inner node access functions
	bool HasInnerNode(const SHAMapNode &id);
	bool GiveInnerNode(SHAMapInnerNode::pointer);
	SHAMapInnerNode::pointer GetInnerNode(const SHAMapNode &);

	// leaf node access functions
	bool HasLeafNode(const SHAMapNode &id);
	bool GiveLeafNode(SHAMapLeafNode::pointer);
	SHAMapLeafNode::pointer GetLeafNode(const SHAMapNode &);

	// generic node functions
	std::vector<unsigned char> getRawNode(const SHAMapNode &id);
	bool addRawNode(const SHAMapNode& nodeID, std::vector<unsigned char> rawNode);

	// normal hash access functions
	bool hasHash(const uint256 &hash);
	bool addHash(const uint256 &hash);
	bool delHash(const uint256 &hash);
	bool firstHash(uint256 &hash);
	bool lastHash(uint256 &hash);
	bool nextHash(uint256 &hash);
	bool prevHash(uint256 &hash);

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode> &nodeHashes, int max);
	void getMissingObjects(std::vector<uint256> &objectHashes, int max);
	bool addKnownNode(const std::vector<unsigned char>& rawNode);

	// overloads for backed maps
	virtual bool fetchNode(const uint256 &hash, std::vector<unsigned char>& rawNode);
	virtual bool writeNode(const uint256 &hash, const std::vector<unsigned char>& rawNode);
	virtual bool haveObject(const uint256 &hash);
};

#endif
