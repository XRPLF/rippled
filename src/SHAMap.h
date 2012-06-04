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
	static uint256 smMasks[65]; // AND with hash to get node id

	uint256	mNodeID;
	int	mDepth;

public:

	static const int rootDepth=0;

	SHAMapNode() : mDepth(0)			{ ; }
	SHAMapNode(int depth, const uint256& hash);
	virtual ~SHAMapNode()				{ ; }
	int getDepth() const				{ return mDepth; }
	const uint256& getNodeID()	const	{ return mNodeID; }
	bool isValid() const { return (mDepth >= 0) && (mDepth < 64); }

	virtual bool isPopulated() const { return false; }

	SHAMapNode getParentNodeID() const
	{
		assert(mDepth);
		return SHAMapNode(mDepth - 1, mNodeID);
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

	static bool ClassInit();
	static uint256 getNodeID(int depth, const uint256& hash);

	// Convert to/from wire format (256-bit nodeID, 1-byte depth)
	void addIDRaw(Serializer &s) const;
	std::string getRawString() const;
	static int getRawIDLength(void) { return 33; }
	SHAMapNode(const void *ptr, int len);
};

extern std::size_t hash_value(const SHAMapNode& mn);
extern std::size_t hash_value(const uint256& u);

class SHAMapItem
{ // an item stored in a SHAMap
public:
	typedef boost::shared_ptr<SHAMapItem> pointer;

private:
	uint256 mTag;
	Serializer mData;

public:

	SHAMapItem(const uint256& tag) : mTag(tag) { ; }
	SHAMapItem(const uint256& tag, const std::vector<unsigned char>& data);
	SHAMapItem(const std::vector<unsigned char>& data); // tag by hash

	const uint256& getTag() const				{ return mTag; }
	std::vector<unsigned char> getData() const	{ return mData.getData(); }
	const std::vector<unsigned char>& peekData() const { return mData.peekData(); }
	Serializer& peekSerializer()				{ return mData; }
	void addRaw(Serializer &s)					{ s.addRaw(mData); }
	void addRaw(std::vector<unsigned char>& s)	{ s.insert(s.end(), mData.begin(), mData.end()); }

	void updateData(const std::vector<unsigned char>& data) { mData=data; }

	bool operator==(const SHAMapItem& i) const		{ return mTag == i.mTag; }
	bool operator!=(const SHAMapItem& i) const		{ return mTag != i.mTag; }
	bool operator==(const uint256& i) const			{ return mTag == i; }
	bool operator!=(const uint256& i) const			{ return mTag != i; }
#if 0
	// This code is comment out because it is unused.  It could work.
	bool operator<(const SHAMapItem& i) const		{ return mTag < i.mTag; }
	bool operator>(const SHAMapItem& i) const		{ return mTag > i.mTag; }
	bool operator<=(const SHAMapItem& i) const		{ return mTag <= i.mTag; }
	bool operator>=(const SHAMapItem& i) const		{ return mTag >= i.mTag; }

	bool operator<(const uint256& i) const			{ return mTag < i; }
	bool operator>(const uint256& i) const			{ return mTag > i; }
	bool operator<=(const uint256& i) const			{ return mTag <= i; }
	bool operator>=(const uint256& i) const			{ return mTag >= i; }
#endif
	virtual void dump();
};

class SHAMapTreeNode : public SHAMapNode
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapTreeNode> pointer;

	enum TNType
	{
		tnERROR			= 0,
		tnINNER			= 1,
		tnTRANSACTION	= 2,
		tnACCOUNT_STATE	= 3
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
	SHAMapTreeNode(uint32 seq, const SHAMapNode& nodeID); // empty node
	SHAMapTreeNode(const SHAMapTreeNode& node, uint32 seq); // copy node from older tree
	SHAMapTreeNode(const SHAMapNode& nodeID, SHAMapItem::pointer item, TNType type, uint32 seq);

	// raw node functions
	SHAMapTreeNode(const SHAMapNode& id, const std::vector<unsigned char>& contents, uint32 seq); // raw node
	void addRaw(Serializer &);

	virtual bool isPopulated() const { return true; }

	// node functions
	uint32 getSeq() const				{ return mSeq; }
	void setSeq(uint32 s)				{ mSeq = s; }
	const uint256& getNodeHash() const	{ return mHash; }
	TNType getType() const				{ return mType; }

	// type functions
	bool isLeaf() const			{ return (mType == tnTRANSACTION) || (mType == tnACCOUNT_STATE); }
	bool isInner() const		{ return mType == tnINNER; }
	bool isValid() const		{ return mType != tnERROR; }
	bool isTransaction() const	{ return mType != tnTRANSACTION; }
	bool isAccountState() const	{ return mType != tnACCOUNT_STATE; }

	// inner node functions
	bool isInnerNode() const { return !mItem; }
	bool setChildHash(int m, const uint256& hash);
	bool isEmptyBranch(int m) const { return !mHashes[m]; }
	int getBranchCount() const;
	void makeInner();
	const uint256& getChildHash(int m) const
	{
		assert((m >= 0) && (m < 16) && (mType == tnINNER));
		return mHashes[m];
	}

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
	void setFullBelow(void)				{ mFullBelow = true; }

	virtual void dump();
	virtual std::string getString() const;
};

enum SHAMapException
{
	MissingNode = 1,
	InvalidNode = 2,
	InvalidMap = 3,
};

enum SHAMapState
{
	Modifying = 0,		// Objects can be added and removed (like an open ledger)
	Immutable = 1,		// Map cannot be changed (like a closed ledger)
	Synching = 2,		// Map's hash is locked in, valid nodes can be added (like a peer's closing ledger)
	Floating = 3,		// Map is free to change hash (like a synching open ledger)
	Invalid = 4,		// Map is known not to be valid (usually synching a corrupt ledger)
};

class SHAMapSyncFilter
{
public:
	SHAMapSyncFilter()				{ ; }
	virtual ~SHAMapSyncFilter()		{ ; }
	virtual void gotNode(const uint256& nodeHash, const std::vector<unsigned char>& nodeData, bool isLeaf)
	{ ; }
	virtual bool haveNode(const uint256&nodeHash, std::vector<unsigned char>& nodeData)
	{ return false; }
};

class SHAMap
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;
	typedef std::map<uint256, std::pair<SHAMapItem::pointer, SHAMapItem::pointer> > SHAMapDiff;

private:
	uint32 mSeq;
	mutable boost::recursive_mutex mLock;
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> mTNByID;

	boost::shared_ptr<std::map<SHAMapNode, SHAMapTreeNode::pointer> > mDirtyNodes;

	SHAMapTreeNode::pointer root;

	SHAMapState mState;

protected:

	void dirtyUp(std::stack<SHAMapTreeNode::pointer>& stack, const uint256& target, uint256 prevHash);
	std::stack<SHAMapTreeNode::pointer> getStack(const uint256& id, bool include_nonmatching_leaf);
	SHAMapTreeNode::pointer walkTo(const uint256& id, bool modify);
	SHAMapTreeNode* walkToPointer(const uint256& id);
	SHAMapTreeNode::pointer checkCacheNode(const SHAMapNode&);
	void returnNode(SHAMapTreeNode::pointer&, bool modify);

	SHAMapTreeNode::pointer getNode(const SHAMapNode& id);
	SHAMapTreeNode::pointer getNode(const SHAMapNode& id, const uint256& hash, bool modify);
	SHAMapTreeNode* getNodePointer(const SHAMapNode& id, const uint256& hash);

	SHAMapItem::pointer firstBelow(SHAMapTreeNode*);
	SHAMapItem::pointer lastBelow(SHAMapTreeNode*);
	SHAMapItem::pointer onlyBelow(SHAMapTreeNode*);
	void eraseChildren(SHAMapTreeNode::pointer);

	bool walkBranch(SHAMapTreeNode* node, SHAMapItem::pointer otherMapItem, bool isFirstMap,
	    SHAMapDiff& differences, int& maxCount);

public:

	// build new map
	SHAMap(uint32 seq = 0);

	// Returns a new map that's a snapshot of this one. Force CoW
	SHAMap::pointer snapShot(bool isMutable);

	// hold the map stable across operations
	ScopedLock Lock() const { return ScopedLock(mLock); }

	bool hasNode(const SHAMapNode& id);

	// normal hash access functions
	bool hasItem(const uint256& id);
	bool delItem(const uint256& id);
	bool addItem(const SHAMapItem& i, bool isTransaction);
	bool updateItem(const SHAMapItem& i, bool isTransaction);
	SHAMapItem getItem(const uint256& id);
	uint256 getHash() const		{ return root->getNodeHash(); }
	uint256 getHash()			{ return root->getNodeHash(); }

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

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max,
		SHAMapSyncFilter* filter);
	bool getNodeFat(const SHAMapNode& node, std::vector<SHAMapNode>& nodeIDs,
	 std::list<std::vector<unsigned char> >& rawNode, bool fatLeaves);
	bool addRootNode(const uint256& hash, const std::vector<unsigned char>& rootNode);
	bool addRootNode(const std::vector<unsigned char>& rootNode);
	bool addKnownNode(const SHAMapNode& nodeID, const std::vector<unsigned char>& rawNode,
		SHAMapSyncFilter* filter);

	// status functions
	void setImmutable(void)		{ assert(mState != Invalid); mState = Immutable; }
	void clearImmutable(void)	{ mState = Modifying; }
	bool isSynching(void) const	{ return (mState == Floating) || (mState == Synching); }
	void setSynching(void)		{ mState = Synching; }
	void setFloating(void)		{ mState = Floating; }
	void clearSynching(void)	{ mState = Modifying; }
	bool isValid(void)			{ return mState != Invalid; }

	// caution: otherMap must be accessed only by this function
	// return value: true=successfully completed, false=too different
	bool compare(SHAMap::pointer otherMap, SHAMapDiff& differences, int maxCount);

	int flushDirty(int maxNodes, HashedObjectType t, uint32 seq);

	void setSeq(uint32 seq)		{ mSeq = seq; }
	uint32 getSeq()				{ return mSeq; }

	// overloads for backed maps
	bool fetchNode(const uint256& hash, std::vector<unsigned char>& rawNode);

	bool operator==(const SHAMap& s) { return getHash() == s.getHash(); }

	// trusted path operations - prove a particular node is in a particular ledger
	std::list<std::vector<unsigned char> > getTrustedPath(const uint256& index);
	static std::vector<unsigned char> checkTrustedPath(const uint256& ledgerHash, const uint256& leafIndex,
		const std::list<std::vector<unsigned char> >& path);

	bool deepCompare(SHAMap& other);
	virtual void dump(bool withHashes = false);
};

#endif
// vim:ts=4
