# SHAMap Introduction #

March 2020

The `SHAMap` is a Merkle tree (http://en.wikipedia.org/wiki/Merkle_tree).
The `SHAMap` is also a radix trie of radix 16
(http://en.wikipedia.org/wiki/Radix_tree).

The Merkle trie data structure is important because subtrees and even the entire
tree can be compared with other trees in O(1) time by simply comparing the hashes.
This makes it very efficient to determine if two `SHAMap`s contain the same set of
transactions or account state modifications.

The radix trie property is helpful in that a key (hash) of a transaction
or account state can be used to navigate the trie.

A `SHAMap` is a trie with two node types:

1.  SHAMapInnerNode
2.  SHAMapTreeNode

Both of these nodes directly inherit from SHAMapAbstractNode which holds data
common to both of the node types.

All non-leaf nodes have type SHAMapInnerNode.

All leaf nodes have type SHAMapTreeNode.

The root node is always a SHAMapInnerNode.

A given `SHAMap` always stores only one of three kinds of data:

 * Transactions with metadata
 * Transactions without metadata, or
 * Account states.

So all of the leaf nodes of a particular `SHAMap` will always have a uniform type.
The inner nodes carry no data other than the hash of the nodes beneath them.

All nodes are owned by shared_ptrs resident in either other nodes, or in case of
the root node, a shared_ptr in the `SHAMap` itself.  The use of shared_ptrs
permits more than one `SHAMap` at a time to share ownership of a node.  This
occurs (for example), when a copy of a `SHAMap` is made.

Copies are made with the `snapShot` function as opposed to the `SHAMap` copy
constructor.  See the section on `SHAMap` creation for more details about
`snapShot`.

Sequence numbers are used to further customize the node ownership strategy. See
the section on sequence numbers for details on sequence numbers.

![node diagram](https://user-images.githubusercontent.com/46455409/77350005-1ef12c80-6cf9-11ea-9c8d-56410f442859.png)

## Mutability ##

There are two different ways of building and using a `SHAMap`:

 1. A mutable `SHAMap` and
 2. An immutable `SHAMap`

The distinction here is not of the classic C++ immutable-means-unchanging sense.
 An immutable `SHAMap` contains *nodes* that are immutable.  Also, once a node has
been located in an immutable `SHAMap`, that node is guaranteed to persist in that
`SHAMap` for the lifetime of the `SHAMap`.

So, somewhat counter-intuitively, an immutable `SHAMap` may grow as new nodes are
introduced.  But an immutable `SHAMap` will never get smaller (until it entirely
evaporates when it is destroyed).  Nodes, once introduced to the immutable
`SHAMap`, also never change their location in memory.  So nodes in an immutable
`SHAMap` can be handled using raw pointers (if you're careful).

One consequence of this design is that an immutable `SHAMap` can never be
"trimmed".  There is no way to identify unnecessary nodes in an immutable `SHAMap`
that could be removed.  Once a node has been brought into the in-memory `SHAMap`,
that node stays in memory for the life of the `SHAMap`.

Most `SHAMap`s are immutable, in the sense that they don't modify or remove their
contained nodes.

An example where a mutable `SHAMap` is required is when we want to apply
transactions to the last closed ledger.  To do so we'd make a mutable snapshot
of the state trie and then start applying transactions to it. Because the
snapshot is mutable, changes to nodes in the snapshot will not affect nodes in
other `SHAMap`s.

An example using a immutable ledger would be when there's an open ledger and
some piece of code wishes to query the state of the ledger.  In this case we
don't wish to change the state of the `SHAMap`, so we'd use an immutable snapshot.

## Sequence numbers ##

Both `SHAMap`s and their nodes carry a sequence number.  This is simply an
unsigned number that indicates ownership or membership, or a non-membership.

`SHAMap`s sequence numbers normally start out as 1.  However when a snap-shot of
a `SHAMap` is made, the copy's sequence number is 1 greater than the original.

The nodes of a `SHAMap` have their own copy of a sequence number.  If the `SHAMap`
is mutable, meaning it can change, then all of its nodes must have the
same sequence number as the `SHAMap` itself.  This enforces an invariant that none
of the nodes are shared with other `SHAMap`s.

When a `SHAMap` needs to have a private copy of a node, not shared by any other
`SHAMap`, it first clones it and then sets the new copy to have a sequence number
equal to the `SHAMap` sequence number.  The `unshareNode` is a private utility
which automates the task of first checking if the node is already sharable, and
if so, cloning it and giving it the proper sequence number.  An example case
where a private copy is needed is when an inner node needs to have a child
pointer altered.  Any modification to a node will require a non-shared node.

When a `SHAMap` decides that it is safe to share a node of its own, it sets the
node's sequence number to 0 (a `SHAMap` never has a sequence number of 0). This
is done for every node in the trie when `SHAMap::walkSubTree` is executed.

Note that other objects in rippled also have sequence numbers (e.g. ledgers).
The `SHAMap` and node sequence numbers should not be confused with these other
sequence numbers (no relation).

## SHAMap Creation ##

A `SHAMap` is usually not created from vacuum.  Once an initial `SHAMap` is
constructed, later `SHAMap`s are usually created by calling snapShot(bool
isMutable) on the original `SHAMap`.  The returned `SHAMap` has the expected
characteristics (mutable or immutable) based on the passed in flag.

It is cheaper to make an immutable snapshot of a `SHAMap` than to make a mutable
snapshot.  If the `SHAMap` snapshot is mutable then sharable nodes must be
copied before they are placed in the mutable map.

A new `SHAMap` is created with each new ledger round.  Transactions not executed
in the previous ledger populate the `SHAMap` for the new ledger.

## Storing SHAMap data in the database ##

When consensus is reached, the ledger is closed.  As part of this process, the
`SHAMap` is stored to the database by calling `SHAMap::flushDirty`.

Both `unshare()` and `flushDirty` walk the `SHAMap` by calling
`SHAMap::walkSubTree`.  As `unshare()` walks the trie, nodes are not written to
the database, and as `flushDirty` walks the trie nodes are written to the
database. `walkSubTree` visits every node in the trie. This process must ensure
that each node is only owned by this trie, and so "unshares" as it walks each
node (from the root down).  This is done in the `preFlushNode` function by
ensuring that the node has a sequence number equal to that of the `SHAMap`.  If
the node doesn't, it is cloned.

For each inner node encountered (starting with the root node), each of the
children are inspected (from 1 to 16).  For each child, if it has a non-zero
sequence number (unshareable), the child is first copied.  Then if the child is
an inner node, we recurse down to that node's children.  Otherwise we've found a
leaf node and that node is written to the database.  A count of each leaf node
that is visited is kept.  The hash of the data in the leaf node is computed at
this time, and the child is reassigned back into the parent inner node just in
case the COW operation created a new pointer to this leaf node.

After processing each node, the node is then marked as sharable again by setting
its sequence number to 0.

After all of an inner node's children are processed, then its hash is updated
and the inner node is written to the database.  Then this inner node is assigned
back into it's parent node, again in case the COW operation created a new
pointer to it.

## Walking a SHAMap ##

The private function `SHAMap::walkTowardsKey` is a good example of *how* to walk
a `SHAMap`, and the various functions that call `walkTowardsKey` are good examples
of *why* one would want to walk a `SHAMap` (e.g. `SHAMap::findKey`).
`walkTowardsKey` always starts at the root of the `SHAMap` and traverses down
through the inner nodes, looking for a leaf node along a path in the trie
designated by a `uint256`.

As one walks the trie, one can *optionally* keep a stack of nodes that one has
passed through.  This isn't necessary for walking the trie, but many clients
will use the stack after finding the desired node.  For example if one is
deleting a node from the trie, the stack is handy for repairing invariants in
the trie after the deletion.

To assist in walking the trie, `SHAMap::walkTowardsKey` uses a `SHAMapNodeID`
that identifies a node by its path from the root and its depth in the trie. The
path is just a "list" of numbers, each in the range [0 .. 15], depicting which
child was chosen at each node starting from the root. Each choice is represented
by 4 bits, and then packed in sequence into a `uint256` (such that the longest
path possible has 256 / 4 = 64 steps). The high 4 bits of the first byte
identify which child of the root is chosen, the lower 4 bits of the first byte
identify the child of that node, and so on. The `SHAMapNodeID` identifying the
root node has an ID of 0 and a depth of 0. See `SHAMapNodeID::selectBranch` for
details of how a `SHAMapNodeID` selects a "branch" (child) by indexing into its
path with its depth.

While the current node is an inner node, traversing down the trie from the root
continues, unless the path indicates a child that does not exist.  And in this
case, `nullptr` is returned to indicate no leaf node along the given path
exists.  Otherwise a leaf node is found and a (non-owning) pointer to it is
returned.  At each step, if a stack is requested, a
`pair<shared_ptr<SHAMapAbstractNode>, SHAMapNodeID>` is pushed onto the stack.

When a child node is found by `selectBranch`, the traversal to that node
consists of two steps:

1.  Update the `shared_ptr` to the current node.
2.  Update the `SHAMapNodeID`.

The first step consists of several attempts to find the node in various places:

1.  In the trie itself.
2.  In the node cache.
3.  In the database.

If the node is not found in the trie, then it is installed into the trie as part
of the traversal process.

## Late-arriving Nodes ##

As we noted earlier, `SHAMap`s (even immutable ones) may grow.  If a `SHAMap` is
searching for a node and runs into an empty spot in the trie, then the `SHAMap`
looks to see if the node exists but has not yet been made part of the map.  This
operation is performed in the `SHAMap::fetchNodeNT()` method.  The *NT*
is this case stands for 'No Throw'.

The `fetchNodeNT()` method goes through three phases:

 1. By calling `getCache()` we attempt to locate the missing node in the
    TreeNodeCache.  The TreeNodeCache is a cache of immutable SHAMapTreeNodes
    that are shared across all `SHAMap`s.

    Any SHAMapTreeNode that is immutable has a sequence number of zero
    (sharable). When a mutable `SHAMap` is created then its SHAMapTreeNodes are
    given non-zero sequence numbers (unsharable).  But all nodes in the
    TreeNodeCache are immutable, so if one is found here, its sequence number
    will be 0.

 2. If the node is not in the TreeNodeCache, we attempt to locate the node
    in the historic data stored by the data base.  The call to to
    `fetchNodeFromDB(hash)` does that work for us.

 3. Finally if a filter exists, we check if it can supply the node.  This is
    typically the LedgerMaster which tracks the current ledger and ledgers
    in the process of closing.

## Canonicalize ##

`canonicalize()` is called every time a node is introduced into the `SHAMap`.

A call to `canonicalize()` stores the node in the `TreeNodeCache` if it does not
already exist in the `TreeNodeCache`.

The calls to `canonicalize()` make sure that if the resulting node is already in
the `SHAMap`, node `TreeNodeCache` or database, then we don't create duplicates
by favoring the copy already in the `TreeNodeCache`.

By using `canonicalize()` we manage a thread race condition where two different
threads might both recognize the lack of a SHAMapTreeNode at the same time
(during a fetch).  If they both attempt to insert the node into the `SHAMap`, then
`canonicalize` makes sure that the first node in wins and the slower thread
receives back a pointer to the node inserted by the faster thread.  Recall
that these two `SHAMap`s will share the same `TreeNodeCache`.

## TreeNodeCache ##

The `TreeNodeCache` is a `std::unordered_map` keyed on the hash of the
`SHAMap` node.  The stored type consists of `shared_ptr<SHAMapAbstractNode>`,
`weak_ptr<SHAMapAbstractNode>`, and a time point indicating the most recent
access of this node in the cache.  The time point is based on
`std::chrono::steady_clock`.

The container uses a cryptographically secure hash that is randomly seeded.

The `TreeNodeCache` also carries with it various data used for statistics
and logging, and a target age for the contained nodes.  When the target age
for a node is exceeded, and there are no more references to the node, the
node is removed from the `TreeNodeCache`.

## FullBelowCache ##

This cache remembers which trie keys have all of their children resident in a
`SHAMap`.  This optimizes the process of acquiring a complete trie.  This is used
when creating the missing nodes list.  Missing nodes are those nodes that a
`SHAMap` refers to but that are not stored in the local database.

As a depth-first walk of a `SHAMap` is performed, if an inner node answers true to
`isFullBelow()` then it is known that none of this node's children are missing
nodes, and thus that subtree does not need to be walked.  These nodes are stored
in the FullBelowCache.  Subsequent walks check the FullBelowCache first when
encountering a node, and ignore that subtree if found.

## SHAMapAbstractNode ##

This is a base class for the two concrete node types.  It holds the following
common data:

1.  A node type, one of:
    a.  error
    b.  inner
    c.  transaction with no metadata
    d.  transaction with metadata
    e.  account state
2.  A hash
3.  A sequence number


## SHAMapInnerNode ##

SHAMapInnerNode publicly inherits directly from SHAMapAbstractNode.  It holds
the following data:

1.  Up to 16 child nodes, each held with a shared_ptr.
2.  A hash for each child.
3.  A 16-bit bitset with a 1 bit set for each child that exists.
4.  Flag to aid online delete and consistency with data on disk.

## SHAMapTreeNode ##

SHAMapTreeNode publicly inherits directly from SHAMapAbstractNode.  It holds the
following data:

1.  A shared_ptr to a const SHAMapItem.

## SHAMapItem ##

This holds the following data:

1.  uint256.  The hash of the data.
2.  vector<unsigned char>.  The data (transactions, account info).

## SHAMap Improvements ##

Here's a simple one: the SHAMapTreeNode::mAccessSeq member is currently not used
and could be removed.

Here's a more important change.  The trie structure is currently embedded in the
SHAMapTreeNodes themselves.  It doesn't have to be that way, and that should be
fixed.

When we navigate the trie (say, like `SHAMap::walkTo()`) we currently ask each
node for information that we could determine locally.  We know the depth because
we know how many nodes we have traversed.  We know the ID that we need because
that's how we're steering.  So we don't need to store the ID in the node.  The
next refactor should remove all calls to `SHAMapTreeNode::GetID()`.

Then we can remove the NodeID member from SHAMapTreeNode.

Then we can change the `SHAMap::mTNBtID`  member to be `mTNByHash`.

An additional possible refactor would be to have a base type, SHAMapTreeNode,
and derive from that InnerNode and LeafNode types.  That would remove some
storage (the array of 16 hashes) from the LeafNodes.  That refactor would also
have the effect of simplifying methods like `isLeaf()` and `hasItem()`.

