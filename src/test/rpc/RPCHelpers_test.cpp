#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

#include <cstdint>
#include <set>
#include <string>

namespace xrpl::test {

class RPCHelpers_test : public beast::unit_test::Suite
{
public:
    void
    testChooseLedgerEntryType()
    {
        testcase("ChooseLedgerEntryType");

        // Test no type.
        json::Value tx = json::ValueType::Object;
        auto result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status::kOK);
        BEAST_EXPECT(result.second == 0);

        // Test empty type.
        tx[jss::type] = "";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status(RpcInvalidParams, "Invalid field 'type'."));
        BEAST_EXPECT(result.second == 0);

        // Test type using canonical name in mixedcase.
        tx[jss::type] = "MPTokenIssuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status::kOK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using canonical name in lowercase.
        tx[jss::type] = "mptokenissuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status::kOK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using RPC name with exact match.
        tx[jss::type] = "mpt_issuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status::kOK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using RPC name with inexact match.
        tx[jss::type] = "MPT_Issuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status(RpcInvalidParams, "Invalid field 'type'."));
        BEAST_EXPECT(result.second == 0);

        // Test invalid type.
        tx[jss::type] = 1234;
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(
            result.first == ::rpc::Status(RpcInvalidParams, "Invalid field 'type', not string."));
        BEAST_EXPECT(result.second == 0);

        // Test unknown type.
        tx[jss::type] = "unknown";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == ::rpc::Status(RpcInvalidParams, "Invalid field 'type'."));
        BEAST_EXPECT(result.second == 0);
    }

    void
    testOwnerDirNodeFieldsCoverage()
    {
        // Every UINT64 sf*Node field must be classified: either it is an
        // owner-directory page hint (rpc::isOwnerDirNodeField()) or it
        // belongs to a book directory (exclusion set below). Adding a new
        // sf*Node UINT64 without updating one of these lists trips this
        // test.
        testcase("Owner-directory Node fields coverage");

        // Book-directory page hints, not owner-directory. sfBookNode: offer
        // book. sfNFTokenOfferNode: NFT bid/ask book.
        std::set<SField const*> const nonOwnerDirNodes{
            &sfBookNode,
            &sfNFTokenOfferNode,
        };

        for (auto const& [_, sf] : SField::getKnownCodeToField())
        {
            if (sf->fieldType != STI_UINT64 || !sf->getName().ends_with("Node"))
                continue;
            BEAST_EXPECTS(
                rpc::isOwnerDirNodeField(*sf) || nonOwnerDirNodes.contains(sf),
                "sf" + sf->getName() +
                    ": add to kOwnerDirNodeFields (owner-dir page hint) "
                    "or to nonOwnerDirNodes (book-dir).");
        }
    }

    void
    testParseAccountMarker()
    {
        testcase("ParseAccountMarker");

        auto const index =
            std::string{"1B8590C01B0006EDFA9ED60296DD052DC5E90F99659B25014D08E1BC983515BC"};
        uint256 expected;
        BEAST_EXPECT(expected.parseHex(index));

        auto const expectParsed = [&](std::string const& marker, std::uint64_t hint) {
            auto const parsed = rpc::parseAccountMarker(marker);
            BEAST_EXPECTS(parsed.has_value(), marker);
            if (parsed.has_value())
            {
                BEAST_EXPECTS(parsed->first == expected, marker);
                BEAST_EXPECTS(parsed->second == hint, marker);
            }
        };

        expectParsed(index + ",7", 7);
        expectParsed(index + ",0", 0);
        expectParsed(index + ",+7", 7);
        expectParsed(index + ",7,ignored", 7);
        expectParsed(index + ",18446744073709551615", 18446744073709551615u);

        for (auto const& marker : {
                 std::string{},
                 index,
                 index + ",",
                 index + ",abc",
                 index + ",7x",
                 index + ",-1",
                 index + ",+",
                 index + ",++7",
                 index + ",18446744073709551616",
                 std::string{"NOTHEX,7"},
                 std::string{",7"},
             })
        {
            BEAST_EXPECTS(!rpc::parseAccountMarker(marker).has_value(), marker);
        }
    }

    void
    run() override
    {
        testChooseLedgerEntryType();
        testOwnerDirNodeFieldsCoverage();
        testParseAccountMarker();
    }
};

BEAST_DEFINE_TESTSUITE(RPCHelpers, rpc, xrpl);

}  // namespace xrpl::test
