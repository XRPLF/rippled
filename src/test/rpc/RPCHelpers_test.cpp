#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/jss.h>

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
        BEAST_EXPECT(result.first == rpc::Status::kOK);
        BEAST_EXPECT(result.second == 0);

        // Test empty type.
        tx[jss::type] = "";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status{RpcInvalidParams});
        BEAST_EXPECT(result.second == 0);

        // Test type using canonical name in mixedcase.
        tx[jss::type] = "MPTokenIssuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status::kOK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using canonical name in lowercase.
        tx[jss::type] = "mptokenissuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status::kOK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using RPC name with exact match.
        tx[jss::type] = "mpt_issuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status::kOK);
        BEAST_EXPECT(result.second == ltMPTOKEN_ISSUANCE);

        // Test type using RPC name with inexact match.
        tx[jss::type] = "MPT_Issuance";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status{RpcInvalidParams});
        BEAST_EXPECT(result.second == 0);

        // Test invalid type.
        tx[jss::type] = 1234;
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status{RpcInvalidParams});
        BEAST_EXPECT(result.second == 0);

        // Test unknown type.
        tx[jss::type] = "unknown";
        result = rpc::chooseLedgerEntryType(tx);
        BEAST_EXPECT(result.first == rpc::Status{RpcInvalidParams});
        BEAST_EXPECT(result.second == 0);
    }

    void
    run() override
    {
        testChooseLedgerEntryType();
    }
};

BEAST_DEFINE_TESTSUITE(RPCHelpers, rpc, xrpl);

}  // namespace xrpl::test
