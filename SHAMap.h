#ifndef __SHAMAP__
#define __SHAMAP__

#include <list>

#include <boost/shared_ptr.hpp>
#include <boost/bimap.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "uint256.h"
#include "ScopedLock.h"

class SHAMap;

// A tree-like map of SHA256 hashes
// 10 levels, consisting of a root, 9 interior node levels, and leaves
// The trees are designed for rapid synchronization and compression of differences


class SHAMapNode
{ // Identifies a node in a SHA256 hash
private:
	static uint256 smMasks[11]; // AND with hash to get node id

	uint256	mNodeID;
	int	mDepth;

public:

	// 0 is root, 10 is leaf
	static const int rootDepth=0;
	static const int leafDepth=10;

	SHAMapNode(int depth, const uint256 &hash);
	int getDepth() { return mDepth; }
	const uint256 &getNodeID() { return mNodeID; }

	bool isRoot() const			{ return mDepth==0; }
	bool isLeaf() const			{ return mDepth==leafDepth; }
	bool isInner() const 		{ return !isRoot() && !isLeaf(); }
	virtual bool IsPopulated(void) const { return false; }

	SHAMapNode getParentNodeID()	{ return SHAMapNode(mDepth-1, mNodeID); }
	SHAMapNode getChildNodeID(int m);
	int selectBranch(const uint256 &hash);

	bool operator<(const SHAMapNode &) const;
	bool operator>(const SHAMapNode &) const;
	bool operator==(const SHAMapNode &) const;
	bool operator!=(const SHAMapNode &) const;
	bool operator<=(const SHAMapNode &) const;
	bool operator>=(const SHAMapNode &) const;

	static void ClassInit();
	static uint256 getNodeID(int depth, const uint256 &hash);
};


class SHAMapItem : public boost::enable_shared_from_this<SHAMapItem>
{ // an item stored in a SHAMap
public:
	typedef boost::shared_ptr<SHAMapItem> pointer;

private:
	uint256 mTag;
	std::vector<unsigned char> mData;

public:
	SHAMapItem(const uint256 &tag, const std::vector<unsigned char>& data);
	SHAMapItem(const std::vector<unsigned char>& data); // tag by hash

	const uint256& getTag(void) const { return mTag; }
	std::vector<unsigned char> getData(void) const { return mData; }
	const std::vector<unsigned char>& peekData(void) const { return mData; }

	void updateData(const std::vector<unsigned char> &data) { mData=data; }

	bool operator<(const SHAMapItem& i) const		{ return mTag<i.mTag; }
	bool operator>(const SHAMapItem& i) const		{ return mTag>i.mTag; }
	bool operator==(const SHAMapItem& i) const		{ return mTag==i.mTag; }
	bool operator!=(const SHAMapItem& i) const		{ return mTag!=i.mTag; }
	bool operator<=(const SHAMapItem& i) const		{ return mTag<=i.mTag; }
	bool operator>=(const SHAMapItem& i) const		{ return mTag>=i.mTag; }
	bool operator<(const uint256& i) const			{ return mTag<i; }
	bool operator>(const uint256& i) const			{ return mTag>i; }
	bool operator==(const uint256& i) const			{ return mTag==i; }
	bool operator!=(const uint256& i) const			{ return mTag!=i; }
	bool operator<=(const uint256& i) const			{ return mTag<=i; }
	bool operator>=(const uint256& i) const			{ return mTag>=i; }
};


class SHAMapLeafNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapLeafNode> pointer;

private:
	uint256	mHash;
	std::list<SHAMapItem> mItems;

	void updateHash();

protected:
	bool addUpdateItem(const SHAMapItem&);
	bool delItem(const SHAMapItem& i) { delItem(i.getTag()); }
	bool delItem(const uint256 &tag);

public:
	SHAMapLeafNode(const SHAMapNode& nodeID);

	virtual bool IsPopulated(void) const { return true; }

	const uint256& GetNodeHash() const	{ return mHash; }
	bool isEmpty() const			{ return mItems.empty(); }
	int getItemCount() const		{ return mItems.size(); }
	const uint256& getHash(int m) const;

	bool hasItem(const uint256 &item) const;
	SHAMapItem::pointer findItem(const uint256 &tag);
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
	void setChildHash(int m, const uint256 &hash);

public:
	SHAMapInnerNode(int Depth, const uint256 &NodeID);

	virtual bool isPopulated(void) const { return true; }
	const uint256& getNodeHash() const  { return mHash; }
	const uint256& getChildHash(int m) const;
	bool isEmpty() const;
};



class SHAMap
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;

private:
	mutable boost::recursive_mutex mLock;
	std::map<SHAMapNode, SHAMapLeafNode> mLeafByID;
	std::map<SHAMapNode, SHAMapInnerNode> mInnerNodeByID;
	boost::bimap<uint256, SHAMapNode> NodeHash;

public:
	SHAMap();

	// hold the map stable across operations
	ScopedLock Lock() const { return ScopedLock(mLock); }

	// inner node access functions
	bool hasInnerNode(const SHAMapNode &id);
	bool giveInnerNode(SHAMapInnerNode::pointer);
	SHAMapInnerNode::pointer getInnerNode(const SHAMapNode &);

	// leaf node access functions
	bool hasLeafNode(const SHAMapNode &id);
	bool giveLeafNode(SHAMapLeafNode::pointer);
	SHAMapLeafNode::pointer getLeafNode(const SHAMapNode &);

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

	// direct mapping
	bool nodeToHash(const SHAMapNode &node, uint256 &hash);
	bool hashToNode(const uint256& hash, SHAMapNode &node);

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode> &nodeHashes, int max);
	void getMissingObjects(std::vector<uint256> &objectHashes, int max);
	bool getNodeFat(const SHAMapNode &node, std::vector<uint256> &nodeHashes, int max);
	bool getNodeFat(const uint256 &hash, std::vector<uint256> &nodeHashes, int max);
	bool addKnownNode(const std::vector<unsigned char>& rawNode);

	// overloads for backed maps
	virtual bool fetchNode(const uint256 &hash, std::vector<unsigned char>& rawNode);
	virtual bool writeNode(const uint256 &hash, const std::vector<unsigned char>& rawNode);
	virtual bool haveObject(const uint256 &hash);

	static bool TestSHAMap();
};

#endif
