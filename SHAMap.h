#ifndef __SHAMAP__
#define __SHAMAP__

#include <list>
#include <map>
#include <stack>

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/unordered/unordered_map.hpp>

#include "types.h"
#include "uint256.h"
#include "ScopedLock.h"
#include "Serializer.h"
#include "HashedObject.h"

class SHAMap;

// A tree-like map of SHA256 hashes
// The trees are designed for rapid synchronization and compression of differences


class SHAMapNode
{ // Identifies a node in a SHA256 hash
public:
	typedef boost::shared_ptr<SHAMapNode> pointer;

private:
	static uint256 smMasks[64]; // AND with hash to get node id

	uint256	mNodeID;
	int	mDepth;

public:

	static const int rootDepth=0;

	SHAMapNode() : mDepth(0)			{ ; }
	SHAMapNode(int depth, const uint256& hash);
	int getDepth() const				{ return mDepth; }
	const uint256& getNodeID()	const	{ return mNodeID; }

	virtual bool isPopulated() const { return false; }

	SHAMapNode getParentNodeID() const
	{
		assert(mDepth);
		return SHAMapNode(mDepth-1, mNodeID);
	}
	SHAMapNode getChildNodeID(int m) const;
	int selectBranch(const uint256& hash) const;

	bool operator<(const SHAMapNode&) const;
	bool operator>(const SHAMapNode&) const;
	bool operator==(const SHAMapNode&) const;
	bool operator==(const uint256&) const;
	bool operator!=(const SHAMapNode&) const;
	bool operator!=(const uint256&) const;
	bool operator<=(const SHAMapNode&) const;
	bool operator>=(const SHAMapNode&) const;
	bool isRoot() const { return mDepth==0; }

	virtual std::string getString() const;
	void dump() const;

	static void ClassInit();
	static uint256 getNodeID(int depth, const uint256& hash);
};

class hash_SMN
{ // These must be randomized for release
public:
	std::size_t operator() (const SHAMapNode& mn) const
	{ return mn.getDepth() ^ static_cast<std::size_t>(mn.getNodeID().PeekAt(0)); }
	std::size_t operator() (const uint256& u) const
	{ return static_cast<std::size_t>(u.PeekAt(0)); }
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

	const uint256& getTag() const { return mTag; }
	std::vector<unsigned char> getData() const { return mData; }
	const std::vector<unsigned char>& peekData() const { return mData; }
	void addRaw(Serializer &s) { s.addRaw(mData); }
	void addRaw(std::vector<unsigned char>& s) { s.insert(s.end(), mData.begin(), mData.end()); }

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
	virtual void dump();
};

class SHAMapTreeNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapTreeNode> pointer;

	enum TNType
	{
		ERROR			=0,
		INNER			=1,
		TRANSACTION		=2,
		ACCOUNT_STATE	=3
	};

private:
	uint256	mHash;
	uint256 mHashes[16];
	SHAMapItem::pointer mItem;
	uint32 mSeq;
	TNType mType;
	bool mFullBelow;

	bool updateHash();

	SHAMapTreeNode(const SHAMapTreeNode&); // no implementation
	SHAMapTreeNode& operator=(const SHAMapTreeNode&); // no implementation

public:
	SHAMapTreeNode(const SHAMapNode& nodeID, uint32 seq); // empty node
	SHAMapTreeNode(const SHAMapTreeNode& node, uint32 seq); // copy node from older tree
	SHAMapTreeNode(const SHAMapNode& nodeID, SHAMapItem::pointer item, TNType type, uint32 seq);

	// raw node functions
	SHAMapTreeNode(const SHAMapNode& id, const std::vector<unsigned char>& contents, uint32 seq); // raw node
	void addRaw(Serializer &);

	virtual bool isPopulated() const { return true; }

	// node functions
	uint32 getSeq() const { return mSeq; }
	void setSeq(uint32 s) { mSeq=s; }
	const uint256& getNodeHash() const	{ return mHash; }
	TNType getType() const { return mType; }

	// type functions
	bool isLeaf() const { return mType==TRANSACTION || mType==ACCOUNT_STATE; }
	bool isInner() const { return mType==INNER; }
	bool isValid() const { return mType!=ERROR; }
	bool isTransaction() const { return mType!=TRANSACTION; }
	bool isAccountState() const { return mType!=ACCOUNT_STATE; }

	// inner node functions
	bool isInnerNode() const { return !mItem; }
	bool setChildHash(int m, const uint256& hash);
	const uint256& getChildHash(int m) const;
	bool isEmptyBranch(int m) const { return !mHashes[m]; }
	int getBranchCount() const;
	void makeInner();

	// item node function
	bool hasItem() const { return !!mItem; }
	SHAMapItem::pointer peekItem() { return mItem; }
	SHAMapItem::pointer getItem() const;
	bool setItem(SHAMapItem::pointer& i, TNType type);
	const uint256& getTag() const { return mItem->getTag(); }
	const std::vector<unsigned char>& peekData() { return mItem->peekData(); }
	std::vector<unsigned char> getData() const { return mItem->getData(); }

	// sync functions
	bool isFullBelow(void) const		{ return mFullBelow; }
	void setFullBelow(void)				{ mFullBelow=true; }

	virtual void dump();
	virtual std::string getString() const;
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
	typedef std::map<uint256, std::pair<SHAMapItem::pointer, SHAMapItem::pointer> > SHAMapDiff;

private:
	uint32 mSeq;
	mutable boost::recursive_mutex mLock;
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer, hash_SMN> mTNByID;

	boost::shared_ptr<std::map<SHAMapNode, SHAMapTreeNode::pointer> > mDirtyNodes;

	SHAMapTreeNode::pointer root;

	bool mImmutable, mSynching;

protected:

	void dirtyUp(std::stack<SHAMapTreeNode::pointer>& stack, const uint256& target, uint256 prevHash);
	std::stack<SHAMapTreeNode::pointer> getStack(const uint256& id, bool include_nonmatching_leaf);
	SHAMapTreeNode::pointer walkTo(const uint256& id, bool modify);
	SHAMapTreeNode::pointer checkCacheNode(const SHAMapNode&);
	void returnNode(SHAMapTreeNode::pointer&, bool modify);

	SHAMapTreeNode::pointer getNode(const SHAMapNode& id);
	SHAMapTreeNode::pointer getNode(const SHAMapNode& id, const uint256& hash, bool modify);

	SHAMapItem::pointer firstBelow(SHAMapTreeNode::pointer);
	SHAMapItem::pointer lastBelow(SHAMapTreeNode::pointer);

	bool walkBranch(SHAMapTreeNode::pointer node, SHAMapItem::pointer otherMapItem, bool isFirstMap,
	    SHAMapDiff& differences, int& maxCount);
	
public:

	// build new map
	SHAMap(uint32 seq=0);

	// hold the map stable across operations
	ScopedLock Lock() const { return ScopedLock(mLock); }

	bool hasNode(const SHAMapNode& id);

	// normal hash access functions
	bool hasItem(const uint256& id);
	bool delItem(const uint256& id);
	bool addItem(const SHAMapItem& i, bool isTransaction);
	bool updateItem(const SHAMapItem& i, bool isTransaction);
	SHAMapItem getItem(const uint256& id);
	uint256 getHash() const { return root->getNodeHash(); }
	uint256 getHash() { return root->getNodeHash(); }

	// save a copy if you have a temporary anyway
	bool updateGiveItem(SHAMapItem::pointer, bool isTransaction);
	bool addGiveItem(SHAMapItem::pointer, bool isTransaction);

	// save a copy if you only need a temporary
	SHAMapItem::pointer peekItem(const uint256& id);

	// traverse functions
	SHAMapItem::pointer peekFirstItem();
	SHAMapItem::pointer peekLastItem();
	SHAMapItem::pointer peekNextItem(const uint256&);
	SHAMapItem::pointer peekPrevItem(const uint256&);

	SHAMapItem::pointer peekPrevItem(const uint160& u) { return peekPrevItem(u.to256()); }
	SHAMapItem::pointer peekNextItem(const uint160& u) { return peekNextItem(u.to256()); }

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max);
	bool getNodeFat(const SHAMapNode& node, std::vector<SHAMapNode>& nodeIDs,
	 std::list<std::vector<unsigned char> >& rawNode);
	bool addRootNode(const uint256& hash, const std::vector<unsigned char>& rootNode);
	bool addRootNode(const std::vector<unsigned char>& rootNode);
	bool addKnownNode(const SHAMapNode& nodeID, const std::vector<unsigned char>& rawNode);

	// status functions
	void setImmutable(void) { mImmutable=true; }
	void clearImmutable(void) { mImmutable=false; }
	void setSynching(void) { mSynching=true; }
	void clearSynching(void) { mSynching=false; }

	// caution: otherMap must be accessed only by this function
	// return value: true=successfully completed, false=too different
	bool compare(SHAMap::pointer otherMap, SHAMapDiff& differences, int maxCount);

	int flushDirty(int maxNodes, HashedObjectType t, uint32 seq);

	void setSeq(uint32 seq) { mSeq=seq; }
	uint32 getSeq() { return mSeq; }

	// overloads for backed maps
	bool fetchNode(const uint256& hash, std::vector<unsigned char>& rawNode);

	bool operator==(const SHAMap& s) { return getHash()==s.getHash(); }

	static bool TestSHAMap();
	static bool syncTest();
	bool deepCompare(SHAMap& other);
	virtual void dump();
};

#endif
