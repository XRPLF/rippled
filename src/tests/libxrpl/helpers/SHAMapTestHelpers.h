#pragma once

#include <xrpl/basics/random.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <cassert>
#include <cstdint>

namespace xrpl::test {

/** Return a randomly-generated SHAMapItem using the supplied engine. */
template <class Engine>
boost::intrusive_ptr<SHAMapItem>
makeRandomSHAMapItem(Engine& engine)
{
    Serializer s;
    for (int d = 0; d < 3; ++d)
        s.add32(randInt<std::uint32_t>(engine));
    return makeShamapitem(s.getSHA512Half(), s.slice());
}

struct NodeCounts
{
    int inner = 0;
    int leaves = 0;
};

/** Count all inner and leaf nodes reachable via visitNodes. */
inline NodeCounts
countNodes(SHAMap const& map)
{
    NodeCounts counts;
    map.visitNodes([&counts](SHAMapTreeNode& node) {
        assert(node.isInner() || node.isLeaf());
        if (node.isInner())
        {
            ++counts.inner;
        }
        else if (node.isLeaf())
        {
            ++counts.leaves;
        }
        return true;
    });
    return counts;
}

}  // namespace xrpl::test
