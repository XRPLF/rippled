# SHAMap Introduction #

July 2014

The SHAMap is a Merkle tree (http://en.wikipedia.org/wiki/Merkle_tree).
The SHAMap is also a radix tree of radix 16
(http://en.wikipedia.org/wiki/Radix_tree).

*We need some kind of sensible summary of the SHAMap here.*

A given SHAMap always stores only one of three kinds of data:

 * Transactions with metadata
 * Transactions without metadata, or
 * Account states.

So all of the leaf nodes of a particular SHAMap will always have a uniform
type.  The inner nodes carry no data other than the hash of the nodes
beneath them.


## SHAMap Types ##

There are two different ways of building and using a SHAMap:

 1. A mutable SHAMap and
 2. An immutable SHAMap

The distinction here is not of the classic C++ immutable-means-unchanging
sense.  An immutable SHAMap contains *nodes* that are immutable.  Also,
once a node has been located in an immutable SHAMap, that node is
guaranteed to persist in that SHAMap for the lifetime of the SHAMap.

So, somewhat counter-intuitively, an immutable SHAMap may grow as new nodes
are introduced.  But an immutable SHAMap will never get smaller (until it
entirely evaporates when it is destroyed).  Nodes, once introduced to the
immutable SHAMap, also never change their location in memory.  So nodes in
an immutable SHAMap can be handled using raw pointers (if you're careful).

One consequence of this design is that an immutable SHAMap can never be
"trimmed".  There is no way to identify unnecessary nodes in an immutable
SHAMap that could be removed.  Once a node has been brought into the
in-memory SHAMap, that node stays in memory for the life of the SHAMap.

Most SHAMaps are immutable, in the sense that they don't modify or remove
their contained nodes.

An example where a mutable SHAMap is required is when we want to apply
transactions to the last closed ledger.  To do so we'd make a mutable
snapshot of the state tree and then start applying transactions to it.
Because the snapshot is mutable, changes to nodes in the snapshot will not
affect nodes in other SHAMAps.

An example using a immutable ledger would be when there's an open ledger
and some piece of code wishes to query the state of the ledger.  In this
case we don't wish to change the state of the SHAMap, so we'd use an
immutable snapshot.


## SHAMap Creation ##

A SHAMap is usually not created from vacuum.  Once an initial SHAMap is
constructed, later SHAMaps are usually created by calling
snapShot(bool isMutable) on the original SHAMap().  The returned SHAMap
has the expected characteristics (mutable or immutable) based on the passed
in flag.

It is cheaper to make an immutable snapshot of a SHAMap than to make a mutable
snapshot.  If the SHAMap snapshot is mutable then any of the nodes that might
be modified must be copied before they are placed in the mutable map.


## SHAMap Thread Safety ##

*This description is obsolete and needs to be rewritten.*

SHAMaps can be thread safe, depending on how they are used.  The SHAMap
uses a SyncUnorderedMap for its storage.  The SyncUnorderedMap has three
thread-safe methods:

 * size(),
 * canonicalize(), and
 * retrieve()

As long as the SHAMap uses only those three interfaces to its storage
(the mTNByID variable [which stands for Tree Node by ID]) the SHAMap is
thread safe.


## Walking a SHAMap ##

*We need a good description of why someone would walk a SHAMap and*
*how it works in the code*


## Late-arriving Nodes ##

As we noted earlier, SHAMaps (even immutable ones) may grow.  If a SHAMap
is searching for a node and runs into an empty spot in the tree, then the
SHAMap looks to see if the node exists but has not yet been made part of
the map.  This operation is performed in the `SHAMap::fetchNodeExternalNT()`
method.  The *NT* is this case stands for 'No Throw'.

The `fetchNodeExternalNT()` method goes through three phases:

 1. By calling `getCache()` we attempt to locate the missing node in the
    TreeNodeCache.  The TreeNodeCache is a cache of immutable
    SHAMapTreeNodes that are shared across all SHAMaps.

    Any SHAMapTreeNode that is immutable has a sequence number of zero.
    When a mutable SHAMap is created then its SHAMapTreeNodes are given
    non-zero sequence numbers.  So the `assert (ret->getSeq() == 0)`
    simply confirms that the TreeNodeCache indeed gave us an immutable node.

 2. If the node is not in the TreeNodeCache, we attempt to locate the node
    in the historic data stored by the data base.  The call to
    to `fetch(hash)` does that work for us.

 3. Finally, if ledgerSeq_ is non-zero and we did't locate the node in the
    historic data, then we call a MissingNodeHandler.

    The non-zero ledgerSeq_ indicates that the SHAMap is a complete map that
    belongs to a historic ledger with the given (non-zero) sequence number.
    So, if all expected data is always present, the MissingNodeHandler should
    never be executed.

    And, since we now know that this SHAMap does not fully represent
    the data from that ledger, we set the SHAMap's sequence number to zero.

If phase 1 returned a node, then we already know that the node is immutable.
However, if either phase 2 executes successfully, then we need to turn the
returned node into an immutable node.  That's handled by the call to
`make_shared<SHAMapTreeNode>` inside the try block.  That code is inside
a try block because the `fetchNodeExternalNT` method promises not to throw.
In case the constructor called by `make_shared` throws we don't want to
break our promise.


## Canonicalize ##

The calls to `canonicalize()` make sure that if the resulting node is already
in the SHAMap, then we return the node that's already present -- we never
replace a pre-existing node.  By using `canonicalize()` we manage a thread
race condition where two different threads might both recognize the lack of a
SHAMapTreeNode at the same time.  If they both attempt to insert the node
then `canonicalize` makes sure that the first node in wins and the slower
thread receives back a pointer to the node inserted by the faster thread.

There's a problem with the current SHAMap design that `canonicalize()`
accommodates.  Two different trees can have the exact same node (the same
hash value) with two different IDs.  If the TreeNodeCache returns a node
with the same hash but a different ID, then we assume that the ID of the
passed-in node is 'better' than the older ID in the TreeNodeCache.  So we
construct a new SHAMapTreeNode by copying the one we found in the
TreeNodeCache, but we give the new node the new ID.  Then we replace the
SHAMapTreeNode in the TreeNodeCache with this newly constructed node.

The TreeNodeCache is not subject to the rule that any node must be
resident forever.  So it's okay to replace the old node with the new node.

The `SHAMap::getCache()` method exhibits the same behavior.


## SHAMap Improvements ##

Here's a simple one: the SHAMapTreeNode::mAccessSeq member is currently not
used and could be removed.

Here's a more important change.  The tree structure is currently embedded
in the SHAMapTreeNodes themselves.  It doesn't have to be that way, and
that should be fixed.

When we navigate the tree (say, like `SHAMap::walkTo()`) we currently
ask each node for information that we could determine locally.  We know
the depth because we know how many nodes we have traversed.  We know the
ID that we need because that's how we're steering.  So we don't need to
store the ID in the node.  The next refactor should remove all calls to
`SHAMapTreeNode::GetID()`.

Then we can remove the NodeID member from SHAMapTreeNode.

Then we can change the SHAMap::mTNBtID  member to be mTNByHash.

An additional possible refactor would be to have a base type, SHAMapTreeNode,
and derive from that InnerNode and LeafNode types.  That would remove
some storage (the array of 16 hashes) from the LeafNodes.  That refactor
would also have the effect of simplifying methods like `isLeaf()` and
`hasItem()`.

