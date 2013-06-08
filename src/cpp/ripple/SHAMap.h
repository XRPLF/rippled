#ifndef RIPPLE_SHAMAP_H
#define RIPPLE_SHAMAP_H

// VFALCO TODO Get this include out of here!
#include "ripple_UptimeTimerAdapter.h"

DEFINE_INSTANCE(SHAMap);
DEFINE_INSTANCE(SHAMapItem);
DEFINE_INSTANCE(SHAMapTreeNode);

class SHAMap;
class SHAMapSyncFilter;

// A tree-like map of SHA256 hashes
// The trees are designed for rapid synchronization and compression of differences

// Identifies a node in a SHA256 hash map
class SHAMapNode
{
public:

	static const int rootDepth = 0;

	SHAMapNode() : mDepth(0), mHash(0)	{ ; }
	SHAMapNode(int depth, const uint256& hash);

	int getDepth() const				{ return mDepth; }
	const uint256& getNodeID()	const	{ return mNodeID; }
	bool isValid() const 				{ return (mDepth >= 0) && (mDepth < 64); }
	bool isRoot() const					{ return mDepth == 0; }
	size_t getMHash() const				{ if (mHash == 0) setMHash(); return mHash; }

	virtual bool isPopulated() const	{ return false; }

	SHAMapNode getParentNodeID() const
	{
		assert(mDepth);
		return SHAMapNode(mDepth - 1, mNodeID);
	}

	SHAMapNode getChildNodeID(int m) const;
	int selectBranch(const uint256& hash) const;

	bool operator<(const SHAMapNode&) const;
	bool operator>(const SHAMapNode&) const;
	bool operator<=(const SHAMapNode&) const;
	bool operator>=(const SHAMapNode&) const;

	bool operator==(const SHAMapNode& n) const	{ return (mDepth == n.mDepth) && (mNodeID == n.mNodeID); }
	bool operator==(const uint256& n) const		{ return n == mNodeID; }
	bool operator!=(const SHAMapNode& n) const	{ return (mDepth != n.mDepth) || (mNodeID != n.mNodeID); }
	bool operator!=(const uint256& n) const		{ return n != mNodeID; }

	virtual std::string getString() const;
	void dump() const;

	static bool ClassInit();
	static uint256 getNodeID(int depth, const uint256& hash);

	// Convert to/from wire format (256-bit nodeID, 1-byte depth)
	void addIDRaw(Serializer &s) const;
	std::string getRawString() const;
	static int getRawIDLength(void) { return 33; }
	SHAMapNode(const void *ptr, int len);

protected:
	SHAMapNode(int depth, const uint256& id, bool) : mNodeID(id), mDepth(depth), mHash(0) { ; }

private:
	static uint256 smMasks[65]; // AND with hash to get node id

	uint256	mNodeID;
	int		mDepth;
	mutable size_t	mHash;

	void setMHash() const;
};

extern std::size_t hash_value(const SHAMapNode& mn);

inline std::ostream& operator<<(std::ostream& out, const SHAMapNode& node) { return out << node.getString(); }

class SHAMapItem : public IS_INSTANCE(SHAMapItem)
{ // an item stored in a SHAMap
public:
	typedef boost::shared_ptr<SHAMapItem>			pointer;
	typedef const boost::shared_ptr<SHAMapItem>&	ref;

private:
	uint256 mTag;
	Serializer mData;

public:

	SHAMapItem(const uint256& tag) : mTag(tag) { ; }
	SHAMapItem(const uint256& tag, Blob const& data);
	SHAMapItem(const uint256& tag, const Serializer& s);
	SHAMapItem(Blob const& data); // tag by hash

	const uint256& getTag() const				{ return mTag; }
	Blob getData() const	{ return mData.getData(); }
	Blob const& peekData() const { return mData.peekData(); }
	Serializer& peekSerializer()				{ return mData; }
	void addRaw(Blob & s) const { s.insert(s.end(), mData.begin(), mData.end()); }

	void updateData(Blob const& data) { mData=data; }

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

enum SHANodeFormat
{
	snfPREFIX	= 1, // Form that hashes to its official hash
	snfWIRE		= 2, // Compressed form used on the wire
	snfHASH		= 3, // just the hash
};

enum SHAMapType
{
	smtTRANSACTION	=1,		// A tree of transactions
	smtSTATE		=2,		// A tree of state nodes
	smtFREE			=3,		// A tree not part of a ledger
};

class SHAMapTreeNode : public SHAMapNode, public IS_INSTANCE(SHAMapTreeNode)
{
	friend class SHAMap;

public:
	typedef boost::shared_ptr<SHAMapTreeNode>			pointer;
	typedef const boost::shared_ptr<SHAMapTreeNode>&	ref;

	enum TNType
	{
		tnERROR				= 0,
		tnINNER				= 1,
		tnTRANSACTION_NM	= 2, // transaction, no metadata
		tnTRANSACTION_MD	= 3, // transaction, with metadata
		tnACCOUNT_STATE		= 4
	};

private:
	uint256				mHash;
	uint256				mHashes[16];
	SHAMapItem::pointer	mItem;
	uint32				mSeq, mAccessSeq;
	TNType				mType;
	int					mIsBranch;
	bool				mFullBelow;

	bool updateHash();

	SHAMapTreeNode(const SHAMapTreeNode&); // no implementation
	SHAMapTreeNode& operator=(const SHAMapTreeNode&); // no implementation

public:
	SHAMapTreeNode(uint32 seq, const SHAMapNode& nodeID); // empty node
	SHAMapTreeNode(const SHAMapTreeNode& node, uint32 seq); // copy node from older tree
	SHAMapTreeNode(const SHAMapNode& nodeID, SHAMapItem::ref item, TNType type, uint32 seq);

	// raw node functions
	SHAMapTreeNode(const SHAMapNode& id, Blob const& data, uint32 seq,
		SHANodeFormat format, const uint256& hash, bool hashValid);
	void addRaw(Serializer &, SHANodeFormat format);

	virtual bool isPopulated() const { return true; }

	// node functions
	uint32 getSeq() const				{ return mSeq; }
	void setSeq(uint32 s)				{ mAccessSeq = mSeq = s; }
	void touch(uint32 s)				{ mAccessSeq = s; }
	const uint256& getNodeHash() const	{ return mHash; }
	TNType getType() const				{ return mType; }

	// type functions
	bool isLeaf() const			{ return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD) ||
									(mType == tnACCOUNT_STATE); }
	bool isInner() const		{ return mType == tnINNER; }
	bool isValid() const		{ return mType != tnERROR; }
	bool isTransaction() const	{ return (mType == tnTRANSACTION_NM) || (mType == tnTRANSACTION_MD); }
	bool hasMetaData() const	{ return mType == tnTRANSACTION_MD; }
	bool isAccountState() const	{ return mType == tnACCOUNT_STATE; }

	// inner node functions
	bool isInnerNode() const		{ return !mItem; }
	bool setChildHash(int m, const uint256& hash);
	bool isEmptyBranch(int m) const	{ return (mIsBranch & (1 << m)) == 0; }
	bool isEmpty() const;
	int getBranchCount() const;
	void makeInner();
	const uint256& getChildHash(int m) const
	{
		assert((m >= 0) && (m < 16) && (mType == tnINNER));
		return mHashes[m];
	}

	// item node function
	bool hasItem() const { return !!mItem; }
	SHAMapItem::ref peekItem() { return mItem; }
	SHAMapItem::pointer getItem() const;
	bool setItem(SHAMapItem::ref i, TNType type);
	const uint256& getTag() const { return mItem->getTag(); }
	Blob const& peekData() { return mItem->peekData(); }
	Blob getData() const { return mItem->getData(); }

	// sync functions
	bool isFullBelow(void) const		{ return mFullBelow; }
	void setFullBelow(void)				{ mFullBelow = true; }

	virtual void dump();
	virtual std::string getString() const;
};

enum SHAMapState
{
	smsModifying = 0,		// Objects can be added and removed (like an open ledger)
	smsImmutable = 1,		// Map cannot be changed (like a closed ledger)
	smsSynching = 2,		// Map's hash is locked in, valid nodes can be added (like a peer's closing ledger)
	smsFloating = 3,		// Map is free to change hash (like a synching open ledger)
	smsInvalid = 4,			// Map is known not to be valid (usually synching a corrupt ledger)
};

class SHAMapSyncFilter
{
public:
	SHAMapSyncFilter()				{ ; }
	virtual ~SHAMapSyncFilter()		{ ; }

	virtual void gotNode(bool fromFilter, const SHAMapNode& id, const uint256& nodeHash,
		Blob const& nodeData, SHAMapTreeNode::TNType type)
	{ ; }

	virtual bool haveNode(const SHAMapNode& id, const uint256& nodeHash, Blob & nodeData)
	{ return false; }
};

class SHAMapMissingNode : public std::runtime_error
{
public:
	SHAMapMissingNode(SHAMapType t, const SHAMapNode& nodeID, const uint256& nodeHash) :
		std::runtime_error("SHAMapMissingNode"), mType(t), mNodeID(nodeID), mNodeHash(nodeHash)
	{ ; }

	SHAMapMissingNode(SHAMapType t, const SHAMapNode& nodeID, const uint256& nodeHash, const uint256& targetIndex) :
		std::runtime_error(nodeID.getString()), mType(t),
		mNodeID(nodeID), mNodeHash(nodeHash), mTargetIndex(targetIndex)
	{ ; }

	virtual ~SHAMapMissingNode() throw()
	{ ; }

	void setTargetNode(const uint256& tn)	{ mTargetIndex = tn; }

	SHAMapType getMapType() const			{ return mType; }
	const SHAMapNode& getNodeID() const		{ return mNodeID; }
	const uint256& getNodeHash() const		{ return mNodeHash; }
	const uint256& getTargetIndex() const	{ return mTargetIndex; }
	bool hasTargetIndex() const				{ return !mTargetIndex.isZero(); }

private:
	SHAMapType mType;
	SHAMapNode mNodeID;
	uint256 mNodeHash;
	uint256 mTargetIndex;
};

extern std::ostream& operator<<(std::ostream&, const SHAMapMissingNode&);

// results of adding nodes
class SMAddNode
{
public:
	SMAddNode() : mInvalid(false), mUseful(false) 		{ ; }

	void setInvalid()		{ mInvalid = true; }
	void setUseful() 		{ mUseful = true; }
	void reset()			{ mInvalid = false; mUseful = false; }

	bool isInvalid() const	{ return mInvalid; }
	bool isUseful() const	{ return mUseful; }

	bool combine(const SMAddNode& n)
	{
		if (n.mInvalid)
		{
			mInvalid = true;
			return false;
		}
		if (n.mUseful)
			mUseful = true;
		return true;
	}

	operator bool() const		{ return !mInvalid; }

	static SMAddNode okay()		{ return SMAddNode(false, false); }
	static SMAddNode useful()	{ return SMAddNode(false, true); }
	static SMAddNode invalid()	{ return SMAddNode(true, false); }

private:
	bool mInvalid, mUseful;

	SMAddNode(bool i, bool u) : mInvalid(i), mUseful(u)	{ ; }
};

// VFALCO TODO tidy up this loose function
extern bool SMANCombine(SMAddNode& existing, const SMAddNode& additional);

class SHAMap : public IS_INSTANCE(SHAMap)
{
public:
	typedef boost::shared_ptr<SHAMap> pointer;
	typedef const boost::shared_ptr<SHAMap>& ref;

	typedef std::pair<SHAMapItem::pointer, SHAMapItem::pointer> SHAMapDiffItem;
	typedef std::map<uint256, SHAMapDiffItem> SHAMapDiff;
	typedef boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> SHADirtyMap;

public:
	// build new map
	SHAMap(SHAMapType t, uint32 seq = 1);
	SHAMap(SHAMapType t, const uint256& hash);

	~SHAMap() { mState = smsInvalid; }

	// Returns a new map that's a snapshot of this one. Force CoW
	SHAMap::pointer snapShot(bool isMutable);

	// Remove nodes from memory
	void dropCache();

	void setLedgerSeq(uint32 lseq) { mLedgerSeq = lseq; }

	// hold the map stable across operations
	ScopedLock Lock() const { return ScopedLock(mLock); }

	bool hasNode(const SHAMapNode& id);
	void fetchRoot(const uint256& hash, SHAMapSyncFilter* filter);

	// normal hash access functions
	bool hasItem(const uint256& id);
	bool delItem(const uint256& id);
	bool addItem(const SHAMapItem& i, bool isTransaction, bool hasMeta);
	bool updateItem(const SHAMapItem& i, bool isTransaction, bool hasMeta);
	SHAMapItem getItem(const uint256& id);
	uint256 getHash() const		{ return root->getNodeHash(); }
	uint256 getHash()			{ return root->getNodeHash(); }

	// save a copy if you have a temporary anyway
	bool updateGiveItem(SHAMapItem::ref, bool isTransaction, bool hasMeta);
	bool addGiveItem(SHAMapItem::ref, bool isTransaction, bool hasMeta);

	// save a copy if you only need a temporary
	SHAMapItem::pointer peekItem(const uint256& id);
	SHAMapItem::pointer peekItem(const uint256& id, uint256& hash);
	SHAMapItem::pointer peekItem(const uint256& id, SHAMapTreeNode::TNType& type);

	// traverse functions
	SHAMapItem::pointer peekFirstItem();
	SHAMapItem::pointer peekFirstItem(SHAMapTreeNode::TNType& type);
	SHAMapItem::pointer peekLastItem();
	SHAMapItem::pointer peekNextItem(const uint256&);
	SHAMapItem::pointer peekNextItem(const uint256&, SHAMapTreeNode::TNType& type);
	SHAMapItem::pointer peekPrevItem(const uint256&);

	// comparison/sync functions
	void getMissingNodes(std::vector<SHAMapNode>& nodeIDs, std::vector<uint256>& hashes, int max,
		SHAMapSyncFilter* filter);
	bool getNodeFat(const SHAMapNode& node, std::vector<SHAMapNode>& nodeIDs,
	 std::list<Blob >& rawNode, bool fatRoot, bool fatLeaves);
	bool getRootNode(Serializer& s, SHANodeFormat format);
	std::vector<uint256> getNeededHashes(int max, SHAMapSyncFilter* filter);
	SMAddNode addRootNode(const uint256& hash, Blob const& rootNode, SHANodeFormat format,
		SHAMapSyncFilter* filter);
	SMAddNode addRootNode(Blob const& rootNode, SHANodeFormat format,
		SHAMapSyncFilter* filter);
	SMAddNode addKnownNode(const SHAMapNode& nodeID, Blob const& rawNode,
		SHAMapSyncFilter* filter);

	// status functions
	void setImmutable()		{ assert(mState != smsInvalid); mState = smsImmutable; }
	void clearImmutable()	{ mState = smsModifying; }
	bool isSynching() const	{ return (mState == smsFloating) || (mState == smsSynching); }
	void setSynching()		{ mState = smsSynching; }
	void setFloating()		{ mState = smsFloating; }
	void clearSynching()	{ mState = smsModifying; }
	bool isValid()			{ return mState != smsInvalid; }

	// caution: otherMap must be accessed only by this function
	// return value: true=successfully completed, false=too different
	bool compare(SHAMap::ref otherMap, SHAMapDiff& differences, int maxCount);

	int armDirty();
	static int flushDirty(SHADirtyMap& dirtyMap, int maxNodes, HashedObjectType t, uint32 seq);
	boost::shared_ptr<SHADirtyMap> disarmDirty();

	void setSeq(uint32 seq)		{ mSeq = seq; }
	uint32 getSeq()				{ return mSeq; }

	// overloads for backed maps
	boost::shared_ptr<SHAMapTreeNode> fetchNodeExternal(const SHAMapNode& id, const uint256& hash);

	bool operator==(const SHAMap& s) { return getHash() == s.getHash(); }

	// trusted path operations - prove a particular node is in a particular ledger
	std::list<Blob > getTrustedPath(const uint256& index);
	static Blob checkTrustedPath(const uint256& ledgerHash, const uint256& leafIndex,
		const std::list<Blob >& path);

	void walkMap(std::vector<SHAMapMissingNode>& missingNodes, int maxMissing);

	bool getPath(const uint256& index, std::vector< Blob >& nodes, SHANodeFormat format);

	bool deepCompare(SHAMap& other);
	virtual void dump(bool withHashes = false);

	typedef std::pair< uint256, Blob > fetchPackEntry_t;
	std::list<fetchPackEntry_t> getFetchPack(SHAMap* have, bool includeLeaves, int max);

	static void sweep()			{ fullBelowCache.sweep(); }

private:
	uint32 mSeq;
	uint32 mLedgerSeq; // sequence number of ledger this is part of
	mutable boost::recursive_mutex mLock;
	boost::unordered_map<SHAMapNode, SHAMapTreeNode::pointer> mTNByID;

	boost::shared_ptr<SHADirtyMap> mDirtyNodes;

	SHAMapTreeNode::pointer root;

	SHAMapState mState;

	SHAMapType mType;

	static KeyCache <uint256, UptimeTimerAdapter> fullBelowCache;

	void dirtyUp(std::stack<SHAMapTreeNode::pointer>& stack, const uint256& target, uint256 prevHash);
	std::stack<SHAMapTreeNode::pointer> getStack(const uint256& id, bool include_nonmatching_leaf, bool partialOk);
	SHAMapTreeNode::pointer walkTo(const uint256& id, bool modify);
	SHAMapTreeNode* walkToPointer(const uint256& id);
	SHAMapTreeNode::pointer checkCacheNode(const SHAMapNode&);
	void returnNode(SHAMapTreeNode::pointer&, bool modify);
	void trackNewNode(SHAMapTreeNode::pointer&);

	SHAMapTreeNode::pointer getNode(const SHAMapNode& id);
	SHAMapTreeNode::pointer getNode(const SHAMapNode& id, const uint256& hash, bool modify);
	SHAMapTreeNode* getNodePointer(const SHAMapNode& id, const uint256& hash);
	SHAMapTreeNode* getNodePointer(const SHAMapNode& id, const uint256& hash, SHAMapSyncFilter* filter);
	SHAMapTreeNode* firstBelow(SHAMapTreeNode*);
	SHAMapTreeNode* lastBelow(SHAMapTreeNode*);

	SHAMapItem::pointer onlyBelow(SHAMapTreeNode*);
	void eraseChildren(SHAMapTreeNode::pointer);
	void dropBelow(SHAMapTreeNode*);
	bool hasInnerNode(const SHAMapNode& nodeID, const uint256& hash);
	bool hasLeafNode(const uint256& tag, const uint256& hash);

	bool walkBranch(SHAMapTreeNode* node, SHAMapItem::ref otherMapItem, bool isFirstMap,
	    SHAMapDiff& differences, int& maxCount);
};

#endif
// vim:ts=4
