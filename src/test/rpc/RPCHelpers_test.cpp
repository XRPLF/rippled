//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.
    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

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
