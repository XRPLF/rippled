#ifndef __SHAMAP__
#define __SHAMAP__

#include <list>
#include <map>

#include <boost/shared_ptr.hpp>
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

	SHAMapNode(int depth, const uint256& hash);
	int getDepth() const		{ return mDepth; }
	const uint256& getNodeID()	{ return mNodeID; }

	bool isRoot() const			{ return mDepth==0; }
	bool isLeaf() const			{ return mDepth==leafDepth; }
	bool isChildLeaf() const	{ return mDepth<(leafDepth-1); }
	bool isInner() const 		{ return !isRoot() && !isLeaf(); }
	virtual bool isPopulated(void) const { return false; }

	SHAMapNode getParentNodeID()	{ return SHAMapNode(mDepth-1, mNodeID); }
	SHAMapNode getChildNodeID(int m);
	int selectBranch(const uint256& hash);

	bool operator<(const SHAMapNode&) const;
	bool operator>(const SHAMapNode&) const;
	bool operator==(const SHAMapNode&) const;
	bool operator!=(const SHAMapNode&) const;
	bool operator<=(const SHAMapNode&) const;
	bool operator>=(const SHAMapNode&) const;

	virtual void dump(void);

	static void ClassInit();
	static uint256 getNodeID(int depth, const uint256& hash);
};


class SHAMapItem : public boost::enable_shared_from_this<SHAMapItem>
{ // an item stored in a SHAMap
public:
	typedef boost::shared_ptr<SHAMapItem> pointer;

private:
	uint256 mTag;
	std::vector<unsigned char> mData;

public:
	SHAMapItem(const uint256& tag); // tag is data
	SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data);
	SHAMapItem(const std::vector<unsigned char>& data); // tag by hash

	const uint256& getTag(void) const { return mTag; }
	std::vector<unsigned char> getData(void) const { return mData; }
	const std::vector<unsigned char>& peekData(void) const { return mData; }

	void updateData(const std::vector<unsigned char>& data) { mData=data; }

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
	virtual void dump(void);
};


class SHAMapLeafNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapLeafNode> pointer;

private:
	uint256	mHash;
	std::list<SHAMapItem::pointer> mItems;

	bool updateHash();

protected:
	bool addUpdateItem(SHAMapItem::pointer);
	bool delItem(const SHAMapItem::pointer i) { delItem(i->getTag()); }
	bool delItem(const uint256& tag);

public:
	SHAMapLeafNode(const SHAMapNode& nodeID);

	virtual bool isPopulated(void) const { return true; }

	const uint256& getNodeHash() const	{ return mHash; }
	bool isEmpty() const			{ return mItems.empty(); }
	int getItemCount() const		{ return mItems.size(); }

	bool hasItem(const uint256& item) const;
	SHAMapItem::pointer findItem(const uint256& tag);
	SHAMapItem::pointer firstItem();
	SHAMapItem::pointer lastItem();
	SHAMapItem::pointer nextItem(SHAMapItem::pointer);

	virtual void dump(void);
};


class SHAMapInnerNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapInnerNode> pointer;

private:
	uint256		mHash;
	uint256		mHashes[32];

	bool updateHash();

protected:
	bool setChildHash(int m, const uint256& hash);

public:
	SHAMapInnerNode(const SHAMapNode& id);
	SHAMapInnerNode(const SHAMapNode& id, const std::vector<unsigned char>& contents);

	virtual bool isPopulated(void) const { return true; }

	bool isEmptyBranch(int m) const		{ return mHashes[m]==0; }
	const uint256& getNodeHash() const  { return mHash; }
	const uint256& getChildHash(int m) const;
	bool isEmpty() const;

	virtual void dump(void);
};


class SHAMap
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;

private:
	int mLeafDataSize, mLeafDataOffset;
	mutable boost::recursive_mutex mLock;
	std::map<SHAMapNode, SHAMapLeafNode::pointer> mLeafByID;
	std::map<SHAMapNode, SHAMapInnerNode::pointer> mInnerNodeByID;
	std::map<SHAMapNode, SHAMapLeafNode::pointer> mDirtyLeafNodes;
	std::map<SHAMapNode, SHAMapInnerNode::pointer> mDirtyInnerNodes;

	SHAMapInnerNode::pointer root;

protected:
	void dirtyUp(const uint256& id, const std::vector<SHAMapInnerNode::pointer>& path);

	SHAMapLeafNode::pointer createLeaf(const SHAMapInnerNode& lowestParent, const uint256& id,
		std::vector<SHAMapInnerNode::pointer>& path);
	SHAMapLeafNode::pointer checkCacheLeaf(const SHAMapNode &);
	SHAMapLeafNode::pointer walkToLeaf(const uint256& id, bool create,
		std::vector<SHAMapInnerNode::pointer>& path);

	SHAMapLeafNode::pointer getLeaf(const SHAMapNode& id, const uint256& hash);
	SHAMapInnerNode::pointer getInner(const SHAMapNode& id, const uint256& hash);

	SHAMapItem::pointer firstBelow(SHAMapInnerNode::pointer);
	SHAMapItem::pointer lastBelow(SHAMapInnerNode::pointer);
	
public:
	SHAMap(int leafDataSize=32, int leafDataOffset=-1);

	// hold the map stable across operations
	ScopedLock Lock() const { return ScopedLock(mLock); }

	// inner node access functions
	bool hasInnerNode(const SHAMapNode& id);
	bool giveInnerNode(SHAMapInnerNode::pointer);
	SHAMapInnerNode::pointer getInnerNode(const SHAMapNode &);

	// leaf node access functions
	bool hasLeafNode(const SHAMapNode& id);
	bool giveLeafNode(SHAMapLeafNode::pointer);
	SHAMapLeafNode::pointer getLeafNode(const SHAMapNode &);

	// generic node functions
	std::vector<unsigned char> getRawNode(const SHAMapNode& id);
	bool addRawNode(const SHAMapNode& nodeID, std::vector<unsigned char> rawNode);

	// normal hash access functions
	bool hasItem(const uint256& id);
	bool delItem(const uint256& id);
	bool addItem(SHAMapItem::pointer item);
	SHAMapItem::pointer getItem(const uint256& id);

	// traverse functions
	SHAMapItem::pointer firstItem();
	SHAMapItem::pointer lastItem();
	SHAMapItem::pointer nextItem(const SHAMapItem &);
	SHAMapItem::pointer prevItem(const SHAMapItem &);

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode>& nodeHashes, int max);
	void getMissingObjects(std::vector<uint256>& objectHashes, int max);
	bool getNodeFat(const SHAMapNode& node, std::vector<uint256>& nodeHashes, int max);
	bool getNodeFat(const uint256& hash, std::vector<uint256>& nodeHashes, int max);
	bool addKnownNode(const std::vector<unsigned char>& rawNode);

	int flushDirty(int maxNodes);

	// overloads for backed maps
	virtual bool fetchNode(const uint256& hash, const SHAMapNode& id, std::vector<unsigned char>& rawNode);
	virtual bool writeNode(const uint256& hash, const SHAMapNode& id, const std::vector<unsigned char>& rawNode);
	virtual void badNode(const uint256& hash, const SHAMapNode& id);

	static bool TestSHAMap();
	virtual void dump(void);
};

#endif
