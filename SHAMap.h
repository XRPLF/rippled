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
// 21 levels, consisting of a root, 19 interior node levels, and leaves
// The trees are designed for rapid synchronization and compression of differences


class SHAMapNode
{ // Identifies a node in a SHA256 hash
public:
	typedef boost::shared_ptr<SHAMapNode> pointer;

private:
	static uint256 smMasks[21]; // AND with hash to get node id

	uint256	mNodeID;
	int	mDepth;

public:

	// 0 is root, 20 is leaf
	static const int rootDepth=0;
	static const int leafDepth=20;

	SHAMapNode(int depth, const uint256& hash);
	int getDepth() const		{ return mDepth; }
	const uint256& getNodeID()	{ return mNodeID; }

	bool isRoot() const			{ return mDepth==0; }
	bool isLeaf() const			{ return mDepth==leafDepth; }
	bool isChildLeaf() const	{ return mDepth==(leafDepth-1); }
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

	virtual std::string getString(void) const;
	void dump(void);

	static void ClassInit();
	static uint256 getNodeID(int depth, const uint256& hash);
};


class SHAMapItem
{ // an item stored in a SHAMap
public:
	typedef boost::shared_ptr<SHAMapItem> pointer;

private:
	uint256 mTag;
	std::vector<unsigned char> mData;

public:

	// for transactions
	SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data);
	SHAMapItem(const std::vector<unsigned char>& data); // tag by hash

	// for account balances
	SHAMapItem(const uint160& tag, const std::vector<unsigned char>& data);

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
	uint32 mSeq;

	bool updateHash();

	SHAMapLeafNode(const SHAMapLeafNode&); // no implementation
	SHAMapLeafNode& operator=(const SHAMapLeafNode&); // no implementation

protected:
	bool addUpdateItem(SHAMapItem::pointer);
	bool delItem(const SHAMapItem::pointer i) { delItem(i->getTag()); }
	bool delItem(const uint256& tag);

public:
	SHAMapLeafNode(const SHAMapNode& nodeID, uint32 seq);
	SHAMapLeafNode(const SHAMapLeafNode& node, uint32 seq);
	SHAMapLeafNode(const SHAMapNode& id, const std::vector<unsigned char>& contents, uint32 seq);

	void addRaw(Serializer &);

	virtual bool isPopulated(void) const { return true; }

	uint32 getSeq(void) const { return mSeq; }
	void setSeq(uint32 s) { mSeq=s; }

	const uint256& getNodeHash() const	{ return mHash; }
	bool isEmpty() const			{ return mItems.empty(); }
	int getItemCount() const		{ return mItems.size(); }

	bool hasItem(const uint256& item) const;
	SHAMapItem::pointer findItem(const uint256& tag);
	SHAMapItem::pointer firstItem();
	SHAMapItem::pointer lastItem();
	SHAMapItem::pointer nextItem(const uint256& tag);
	SHAMapItem::pointer prevItem(const uint256& tag);

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
	uint32		mSeq;

	bool updateHash();

	SHAMapInnerNode(const SHAMapInnerNode&); // no implementation
	SHAMapInnerNode& operator=(const SHAMapInnerNode&); // no implementation

protected:
	bool setChildHash(int m, const uint256& hash);

public:
	SHAMapInnerNode(const SHAMapNode& id, uint32 seq);
	SHAMapInnerNode(const SHAMapInnerNode& node, uint32 seq);
	SHAMapInnerNode(const SHAMapNode& id, const std::vector<unsigned char>& contents, uint32 seq);

	void addRaw(Serializer&);

	uint32 getSeq(void) const { return mSeq; }
	void setSeq(uint32 s) { mSeq=s; }

	virtual bool isPopulated(void) const { return true; }

	bool isEmptyBranch(int m) const		{ return mHashes[m]==0; }
	const uint256& getNodeHash() const  { return mHash; }
	const uint256& getChildHash(int m) const;
	bool isEmpty() const;

	virtual void dump(void);
};

enum SHAMapException
{
	MissingNode=1,
	InvalidNode=2
};


class SHAMap
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;

private:
	uint32 mSeq;
	mutable boost::recursive_mutex mLock;
	std::map<SHAMapNode, SHAMapLeafNode::pointer> mLeafByID;
	std::map<SHAMapNode, SHAMapInnerNode::pointer> mInnerNodeByID;
	boost::shared_ptr<std::map<SHAMapNode, SHAMapLeafNode::pointer> > mDirtyLeafNodes;
 	boost::shared_ptr<std::map<SHAMapNode, SHAMapInnerNode::pointer> > mDirtyInnerNodes;

	SHAMapInnerNode::pointer root;

protected:
	void dirtyUp(const uint256& id);

	SHAMapLeafNode::pointer createLeaf(const SHAMapInnerNode& lowestParent, const uint256& id);
	SHAMapLeafNode::pointer checkCacheLeaf(const SHAMapNode&);
	SHAMapLeafNode::pointer walkToLeaf(const uint256& id, bool create, bool modify);

	SHAMapLeafNode::pointer getLeaf(const SHAMapNode& id, const uint256& hash, bool modify);
	SHAMapLeafNode::pointer returnLeaf(SHAMapLeafNode::pointer leaf, bool modify);
	SHAMapInnerNode::pointer getInner(const SHAMapNode& id, const uint256& hash, bool modify);
	SHAMapInnerNode::pointer returnNode(SHAMapInnerNode::pointer node, bool modify);

	SHAMapItem::pointer firstBelow(SHAMapInnerNode::pointer);
	SHAMapItem::pointer lastBelow(SHAMapInnerNode::pointer);
	
public:

	// build new map
	SHAMap();

	// hold the map stable across operations
	ScopedLock Lock() const { return ScopedLock(mLock); }

	// inner node access functions
	bool hasInnerNode(const SHAMapNode& id);
	bool giveInnerNode(SHAMapInnerNode::pointer);
	SHAMapInnerNode::pointer getInnerNode(const SHAMapNode&);

	// leaf node access functions
	bool hasLeafNode(const SHAMapNode& id);
	bool giveLeafNode(SHAMapLeafNode::pointer);
	SHAMapLeafNode::pointer getLeafNode(const SHAMapNode&);

	// generic node functions
	std::vector<unsigned char> getRawNode(const SHAMapNode& id);
	bool addRawNode(const SHAMapNode& nodeID, std::vector<unsigned char> rawNode);

	// normal hash access functions
	bool hasItem(const uint256& id);
	bool delItem(const uint256& id);
	bool addItem(const SHAMapItem& i);
	bool updateItem(const SHAMapItem& i);
	SHAMapItem getItem(const uint256& id);

	// save a copy if you have a temporary anyway
	bool updateGiveItem(SHAMapItem::pointer);
	bool addGiveItem(SHAMapItem::pointer);

	// save a copy if you only need a temporary
	SHAMapItem::pointer peekItem(const uint256& id);

	// traverse functions
	SHAMapItem::pointer peekFirstItem();
	SHAMapItem::pointer peekLastItem();
	SHAMapItem::pointer peekNextItem(const uint256&);
	SHAMapItem::pointer peekPrevItem(const uint256&);

	SHAMapItem::pointer peekPrevItem(const uint160& u) { return peekPrevItem(uint160to256(u)); }
	SHAMapItem::pointer peekNextItem(const uint160& u) { return peekNextItem(uint160to256(u)); }

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode>& nodeHashes, int max);
	void getMissingObjects(std::vector<uint256>& objectHashes, int max);
	bool getNodeFat(const SHAMapNode& node, std::vector<uint256>& nodeHashes, int max);
	bool getNodeFat(const uint256& hash, std::vector<uint256>& nodeHashes, int max);
	bool addKnownNode(const std::vector<unsigned char>& rawNode);

	int flushDirty(int maxNodes);

	// overloads for backed maps
	virtual bool fetchInnerNode(const uint256& hash, const SHAMapNode& id, std::vector<unsigned char>& rawNode);
	virtual bool fetchLeafNode(const uint256& hash, const SHAMapNode& id, std::vector<unsigned char>& rawNode);
	virtual bool writeInnerNode(const uint256& hash, const SHAMapNode& id, const std::vector<unsigned char>& rawNode);
	virtual bool writeLeafNode(const uint256& hash, const SHAMapNode& id, const std::vector<unsigned char>& rawNode);

	static bool TestSHAMap();
	virtual void dump(void);
};

#endif
