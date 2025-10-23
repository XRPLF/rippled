#ifndef XRPL_SHAMAP_TREENODECACHE_H_INCLUDED
#define XRPL_SHAMAP_TREENODECACHE_H_INCLUDED

#include <xrpld/shamap/SHAMapTreeNode.h>

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/TaggedCache.h>

namespace ripple {

using TreeNodeCache = TaggedCache<
    uint256,
    SHAMapTreeNode,
    /*IsKeyCache*/ false,
    intr_ptr::SharedWeakUnionPtr<SHAMapTreeNode>,
    intr_ptr::SharedPtr<SHAMapTreeNode>>;
}  // namespace ripple

#endif
