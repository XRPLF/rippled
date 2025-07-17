//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <test/jtx.h>

#include <xrpld/app/misc/WasmHostFuncImpl.h>

namespace ripple {
namespace test {

struct WasmHostFuncImpl_test : public beast::unit_test::suite
{
    void
    testCacheLedgerObj()
    {
        testcase("cacheLedgerObj");
        using namespace test::jtx;

        Env env{*this};
        OpenView ov{*env.current()};
        STTx tx{ttESCROW_FINISH, [](STObject&) {}};
        test::StreamSink sink{beast::severities::kWarning};
        beast::Journal jlog{sink};
        ApplyContext ac{
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog};
        auto const dummyEscrow = keylet::escrow(env.master, 2);
        auto const accountKeylet = keylet::account(env.master);
        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, -1).error() ==
                HostFunctionError::SLOT_OUT_RANGE);
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 257).error() ==
                HostFunctionError::SLOT_OUT_RANGE);
            BEAST_EXPECT(
                hfs.cacheLedgerObj(dummyEscrow.key, 0).error() ==
                HostFunctionError::LEDGER_OBJ_NOT_FOUND);

            for (int i = 0; i < 256; ++i)
            {
                auto const result = hfs.cacheLedgerObj(accountKeylet.key, i);
                BEAST_EXPECT(result.value() == i);
            }
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 256).error() ==
                HostFunctionError::SLOTS_FULL);
        }

        {
            WasmHostFunctionsImpl hfs(ac, dummyEscrow);

            for (int i = 0; i < 256; ++i)
            {
                auto const result = hfs.cacheLedgerObj(accountKeylet.key, 0);
                BEAST_EXPECT(result.value() == i);
            }
            BEAST_EXPECT(
                hfs.cacheLedgerObj(accountKeylet.key, 256).error() ==
                HostFunctionError::SLOTS_FULL);
        }
    }

    void
    run() override
    {
        testCacheLedgerObj();
    }
};

BEAST_DEFINE_TESTSUITE(WasmHostFuncImpl, app, ripple);

}  // namespace test
}  // namespace ripple
