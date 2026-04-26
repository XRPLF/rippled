#include <xrpld/app/ledger/detail/LedgerNodeHelpers.h>

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

#include <bit>
#include <cstdint>
#include <string>

namespace xrpl::tests {

class LedgerNodeHelpers_test : public beast::unit_test::suite
{
    static boost::intrusive_ptr<SHAMapItem>
    makeTestItem(std::uint32_t seed)
    {
        Serializer s;
        s.add32(seed);
        s.add32(seed + 1);
        s.add32(seed + 2);
        return make_shamapitem(s.getSHA512Half(), s.slice());
    }

    static std::string
    serializeNode(SHAMapTreeNodePtr const& node)
    {
        Serializer s;
        node->serializeForWire(s);
        auto const slice = s.slice();
        return std::string(std::bit_cast<char const*>(slice.data()), slice.size());
    }

    void
    testValidateLedgerNode()
    {
        // In the tests below the validity of the content of the node data and ID fields is not
        // checked - only that the fields have values when expected. The content of the fields is
        // verified in the other tests in this file.
        testcase("validateLedgerNode");

        // Invalid: missing all fields.
        {
            protocol::TMLedgerNode const node;
            BEAST_EXPECT(!validateLedgerNode(node));
        }

        // Invalid: missing `nodedata` field.
        {
            protocol::TMLedgerNode node;
            node.set_nodeid("test_nodeid");
            BEAST_EXPECT(!validateLedgerNode(node));
        }

        // Invalid: missing `nodedata` field.
        {
            protocol::TMLedgerNode node;
            node.set_id("test_nodeid");
            BEAST_EXPECT(!validateLedgerNode(node));
        }

        // Invalid: missing `nodedata` field.
        {
            protocol::TMLedgerNode node;
            node.set_depth(1);
            BEAST_EXPECT(!validateLedgerNode(node));
        }

        // Valid: legacy `nodeid` field.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_nodeid("test_nodeid");
            BEAST_EXPECT(validateLedgerNode(node));
        }

        // Invalid: has both legacy `nodeid` and new `id` fields.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_nodeid("test_nodeid");
            node.set_id("test_nodeid");
            BEAST_EXPECT(!validateLedgerNode(node));
        }

        // Invalid: has both legacy `nodeid` and new `depth` fields.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_nodeid("test_nodeid");
            node.set_depth(5);
            BEAST_EXPECT(!validateLedgerNode(node));
        }

        // Valid: new `id` field.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_id("test_id");
            BEAST_EXPECT(validateLedgerNode(node));
        }

        // Valid: new `depth` field.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_depth(5);
            BEAST_EXPECT(validateLedgerNode(node));
        }

        // Valid: `depth` at minimum depth.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_depth(0);
            BEAST_EXPECT(validateLedgerNode(node));
        }

        // Valid: `depth` at arbitrary depth between minimum and maximum.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_depth(10);
            BEAST_EXPECT(validateLedgerNode(node));
        }

        // Valid: `depth` at maximum depth.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_depth(SHAMap::leafDepth);
            BEAST_EXPECT(validateLedgerNode(node));
        }

        // Invalid: `depth` is greater than maximum depth.
        {
            protocol::TMLedgerNode node;
            node.set_nodedata("test_data");
            node.set_depth(SHAMap::leafDepth + 1);
            BEAST_EXPECT(!validateLedgerNode(node));
        }
    }

    void
    testGetTreeNode()
    {
        testcase("getTreeNode");

        // Valid: inner node. It must have at least one child for `serializeNode` to work.
        {
            auto const innerNode = intr_ptr::make_shared<SHAMapInnerNode>(1);
            auto const childNode = intr_ptr::make_shared<SHAMapInnerNode>(1);
            innerNode->setChild(0, childNode);
            auto const innerData = serializeNode(innerNode);
            auto const result = getTreeNode(innerData);
            BEAST_EXPECT(result && result->isInner());
        }

        // Valid: leaf node.
        {
            auto const leafItem = makeTestItem(12345);
            auto const leafNode = intr_ptr::make_shared<SHAMapAccountStateLeafNode>(leafItem, 1);
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
            auto const leafNode = intr_ptr::make_shared<SHAMapAccountStateLeafNode>(leafItem, 1);
            // Truncate the data to trigger an exception in SHAMapTreeNode::makeAccountState when
            // the data is used to deserialize the node.
            uint256 const tag;
            auto const leafData = serializeNode(leafNode).substr(0, tag.bytes - 1);
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
            auto const innerNode = intr_ptr::make_shared<SHAMapInnerNode>(1);
            auto const childNode = intr_ptr::make_shared<SHAMapInnerNode>(1);
            innerNode->setChild(0, childNode);
            auto const innerData = serializeNode(innerNode);

            // Valid: legacy `nodeid` field at arbitrary depth.
            {
                auto const innerDepth = 3;
                auto const innerID = SHAMapNodeID::createID(innerDepth, uint256{});

                protocol::TMLedgerNode node;
                node.set_nodedata(innerData);
                node.set_nodeid(innerID.getRawString());
                auto const result = getSHAMapNodeID(node, innerNode);
                BEAST_EXPECT(result == innerID);
            }

            // Valid: new `id` field at minimum depth.
            {
                auto const innerDepth = 0;
                auto const innerID = SHAMapNodeID::createID(innerDepth, uint256{});

                protocol::TMLedgerNode node;
                node.set_nodedata(innerData);
                node.set_id(innerID.getRawString());
                auto const result = getSHAMapNodeID(node, innerNode);
                BEAST_EXPECT(result == innerID);
            }

            // Invalid: new `depth` field should not be used for inner nodes.
            {
                protocol::TMLedgerNode node;
                node.set_nodedata(innerData);
                node.set_depth(10);
                auto const result = getSHAMapNodeID(node, innerNode);
                BEAST_EXPECT(!result);
            }
        }

        {
            // Tests using leaf nodes at various depths.
            auto const leafItem = makeTestItem(12345);
            auto const leafNode = intr_ptr::make_shared<SHAMapAccountStateLeafNode>(leafItem, 1);
            auto const leafData = serializeNode(leafNode);
            auto const leafKey = leafItem->key();

            // Valid: legacy `nodeid` field at arbitrary depth.
            {
                auto const leafDepth = 5;
                auto const leafID = SHAMapNodeID::createID(leafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_nodeid(leafID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Invalid: new `id` field should not be used for leaf nodes.
            {
                auto const leafDepth = 5;
                auto const leafID = SHAMapNodeID::createID(leafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_id(leafID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, leafNode);
                BEAST_EXPECT(!result);
            }

            // Valid: new `depth` field at minimum depth.
            {
                auto const leafDepth = 0;
                auto const leafID = SHAMapNodeID::createID(leafDepth, leafKey);

                protocol::TMLedgerNode node;
                node.set_nodedata(leafData);
                node.set_depth(leafDepth);
                auto const result = getSHAMapNodeID(node, leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Valid: new `depth` field at arbitrary depth between minimum and maximum.
            {
                auto const leafDepth = 10;
                auto const leafID = SHAMapNodeID::createID(leafDepth, leafKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(leafData);
                ledgerNode.set_depth(leafDepth);
                auto const result = getSHAMapNodeID(ledgerNode, leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Valid: new `depth` field at maximum depth.
            // Note that we do not test a depth greater than the maximum depth, because the proto
            // message is assumed to have been validated by the time the getSHAMapNodeID function is
            // called.
            {
                auto const leafDepth = SHAMap::leafDepth;
                auto const leafID = SHAMapNodeID::createID(leafDepth, leafKey);

                protocol::TMLedgerNode node;
                node.set_nodedata(leafData);
                node.set_depth(leafDepth);
                auto const result = getSHAMapNodeID(node, leafNode);
                BEAST_EXPECT(result == leafID);
            }

            // Invalid: legacy `nodeid` field where the node ID is inconsistent with the key.
            {
                auto const otherItem = makeTestItem(54321);
                auto const otherNode =
                    intr_ptr::make_shared<SHAMapAccountStateLeafNode>(otherItem, 1);
                auto const otherData = serializeNode(otherNode);
                auto const otherKey = otherItem->key();
                auto const otherDepth = 1;
                auto const otherID = SHAMapNodeID::createID(otherDepth, otherKey);

                protocol::TMLedgerNode ledgerNode;
                ledgerNode.set_nodedata(otherData);
                ledgerNode.set_nodeid(otherID.getRawString());
                auto const result = getSHAMapNodeID(ledgerNode, leafNode);
                BEAST_EXPECT(!result);
            }
        }
    }

public:
    void
    run() override
    {
        testValidateLedgerNode();
        testGetTreeNode();
        testGetSHAMapNodeID();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerNodeHelpers, app, xrpl);

}  // namespace xrpl::tests
