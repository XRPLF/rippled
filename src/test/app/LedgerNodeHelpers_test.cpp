#include <xrpld/app/ledger/LedgerNodeHelpers.h>

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapAccountStateLeafNode.h>
#include <xrpl/shamap/SHAMapInnerNode.h>
#include <xrpl/shamap/SHAMapItem.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <xrpl.pb.h>

#include <cstdint>
#include <string>

namespace xrpl::tests {

class LedgerNodeHelpers_test : public beast::unit_test::Suite
{
    static boost::intrusive_ptr<SHAMapItem>
    makeTestItem(std::uint32_t seed)
    {
        Serializer s;
        s.add32(seed);
        s.add32(seed + 1);
        s.add32(seed + 2);
        return makeShamapitem(s.getSHA512Half(), s.slice());
    }

    static std::string
    serializeNode(SHAMapTreeNodePtr const& node)
    {
        Serializer s;
        node->serializeForWire(s);
        auto const slice = s.slice();
        return std::string(slice.begin(), slice.end());
    }

    void
    testGetTreeNode()
    {
        testcase("getTreeNode");

        // Valid: inner node. It must have at least one child for `serializeNode` to work.
        {
            auto const innerNode = intr_ptr::makeShared<SHAMapInnerNode>(1);
            auto const childNode = intr_ptr::makeShared<SHAMapInnerNode>(1);
            innerNode->setChild(0, childNode);
            auto const innerData = serializeNode(innerNode);
            auto const result = getTreeNode(innerData);
            BEAST_EXPECT(result && result->isInner());
        }

        // Valid: leaf node.
        {
            auto const leafItem = makeTestItem(12345);
            auto const leafNode = intr_ptr::makeShared<SHAMapAccountStateLeafNode>(leafItem, 1);
            auto const leafData = serializeNode(leafNode);
            auto const result = getTreeNode(leafData);
            BEAST_EXPECT(result && result->isLeaf());
        }

        // Invalid: empty data.
        {
            auto const result = getTreeNode("");
            BEAST_EXPECT(!result);
        }

        // Invalid: garbage data.
        {
            auto const result = getTreeNode("invalid");
            BEAST_EXPECT(!result);
        }

        // Invalid: truncated data.
        {
            auto const leafItem = makeTestItem(54321);
            auto const leafNode = intr_ptr::makeShared<SHAMapAccountStateLeafNode>(leafItem, 1);
            // Truncate the data to trigger an exception in SHAMapTreeNode::makeAccountState when
            // the data is used to deserialize the node.
            uint256 const tag;
            auto const leafData = serializeNode(leafNode).substr(0, tag.kBytes - 1);
            auto const result = getTreeNode(leafData);
            BEAST_EXPECT(!result);
        }
    }

    void
    testGetSHAMapNodeID()
    {
        testcase("getSHAMapNodeID");

        {
            // Tests using inner nodes at various depths.
            auto const innerNode = intr_ptr::makeShared<SHAMapInnerNode>(1);
            auto const childNode = intr_ptr::makeShared<SHAMapInnerNode>(1);
            innerNode->setChild(0, childNode);
            auto const innerData = serializeNode(innerNode);

            // Valid: legacy `nodeid` field at arbitrary depth.
            {
                auto const innerDepth = 3;
                auto const innerID = SHAMapNodeID::createID(innerDepth, uint256{});

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(innerData);
                ledgerNode.set_nodeid(innerID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, *innerNode);
                BEAST_EXPECT(result == innerID);
            }

            // Valid: new `id` field at minimum depth.
            {
                auto const innerDepth = 0;
                auto const innerID = SHAMapNodeID::createID(innerDepth, uint256{});

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(innerData);
                ledgerNode.set_id(innerID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, *innerNode);
                BEAST_EXPECT(result == innerID);
            }

            // Invalid: new `depth` field should not be used for inner nodes.
            {
                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(innerData);
                ledgerNode.set_depth(10);
                auto const result = getSHAMapNodeID(ledgerNode, *innerNode);
                BEAST_EXPECT(!result);
            }

            // Invalid: both legacy `nodeid` and new `id` fields set for an inner node.
            {
                auto const innerDepth = 9;
                auto const innerID = SHAMapNodeID::createID(innerDepth, uint256{});

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(innerData);
                ledgerNode.set_nodeid(innerID.getRawString());
                ledgerNode.set_id(innerID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, *innerNode);
                BEAST_EXPECT(!result);
            }
        }

        {
            // Tests using leaf nodes at various depths.
            auto const leafItem = makeTestItem(12345);
            auto const leafNode = intr_ptr::makeShared<SHAMapAccountStateLeafNode>(leafItem, 1);
            auto const leafData = serializeNode(leafNode);
            auto const leafKey = leafItem->key();

            // Valid: legacy `nodeid` field at arbitrary depth.
            {
                auto const kLeafDepth = 5;
                auto const leafID = SHAMapNodeID::createID(kLeafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_nodeid(leafID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, *leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Invalid: new `id` field should not be used for leaf nodes.
            {
                auto const kLeafDepth = 5;
                auto const leafID = SHAMapNodeID::createID(kLeafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_id(leafID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, *leafNode);
                BEAST_EXPECT(!result);
            }

            // Valid: new `depth` field at minimum depth.
            {
                auto const kLeafDepth = 0;
                auto const leafID = SHAMapNodeID::createID(kLeafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_depth(kLeafDepth);
                auto const result = getSHAMapNodeID(ledgerNode, *leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Valid: new `depth` field at arbitrary depth between minimum and maximum.
            {
                auto const kLeafDepth = 10;
                auto const leafID = SHAMapNodeID::createID(kLeafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_depth(kLeafDepth);
                auto const result = getSHAMapNodeID(ledgerNode, *leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Valid: new `depth` field at maximum depth.
            // Note that we do not test a depth greater than the maximum depth, because the proto
            // message is assumed to have been validated by the time the getSHAMapNodeID function is
            // called.
            {
                auto const kLeafDepth = SHAMap::kLeafDepth;
                auto const leafID = SHAMapNodeID::createID(kLeafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_depth(kLeafDepth);
                auto const result = getSHAMapNodeID(ledgerNode, *leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Invalid: legacy `nodeid` field where the node ID is inconsistent with the key.
            {
                auto const otherItem = makeTestItem(54321);
                auto const otherNode =
                    intr_ptr::makeShared<SHAMapAccountStateLeafNode>(otherItem, 1);
                auto const otherData = serializeNode(otherNode);
                auto const otherKey = otherItem->key();
                auto const otherDepth = 1;
                auto const otherID = SHAMapNodeID::createID(otherDepth, otherKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(otherData);
                ledgerNode.set_nodeid(otherID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, *leafNode);
                BEAST_EXPECT(!result);
            }
        }

        // Invalid: no field set.
        {
            auto const innerNode = intr_ptr::makeShared<SHAMapInnerNode>(1);
            protocol::TMLedgerNode ledgerNode;
            ledgerNode.set_nodedata("test_data");
            auto const result = getSHAMapNodeID(ledgerNode, *innerNode);
            BEAST_EXPECT(!result);
        }
    }

public:
    void
    run() override
    {
        testGetTreeNode();
        testGetSHAMapNodeID();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerNodeHelpers, app, xrpl);

}  // namespace xrpl::tests
