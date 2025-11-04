#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {

class RPCHelpers_test : public beast::unit_test::suite
{
public:
    void
    testChooseLedgerEntryType()
    {
        testcase("ChooseLedgerEntryType");

        // Test no type.
        Json::Value tx = Json::objectValue;
        auto result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status::OK);
        BEAST_EXPECT(result.second == 0);

        // Test empty type.
        tx[jss::type] = "";
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status{rpcINVALID_PARAMS});
        BEAST_EXPECT(result.second == 0);

        // Test type using canonical name in mixedcase.
        tx[jss::type] = "MPTokenIssuance";
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status::OK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using canonical name in lowercase.
        tx[jss::type] = "mptokenissuance";
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status::OK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using RPC name with exact match.
        tx[jss::type] = "mpt_issuance";
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status::OK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using RPC name with inexact match.
        tx[jss::type] = "MPT_Issuance";
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status{rpcINVALID_PARAMS});
        BEAST_EXPECT(result.second == 0);

        // Test invalid type.
        tx[jss::type] = 1234;
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status{rpcINVALID_PARAMS});
        BEAST_EXPECT(result.second == 0);

        // Test unknown type.
        tx[jss::type] = "unknown";
        result = RPC::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == RPC::Status{rpcINVALID_PARAMS});
        BEAST_EXPECT(result.second == 0);
    }

    void
    run() override
    {
        testChooseLedgerEntryType();
    }
};

BEAST_DEFINE_TESTSUITE(RPCHelpers, rpc, ripple);

}  // namespace test
}  // namespace ripple
