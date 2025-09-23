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

#include <test/app/wasm_fixtures/fixtures.h>
#include <test/jtx.h>

#include <xrpld/app/wasm/HostFunc.h>
#include <xrpld/app/wasm/WasmVM.h>


namespace ripple {

namespace test {

struct TestLedgerDataProvider : public HostFunctions
{
    jtx::Env* env_;
    void const* rt_ = nullptr;

public:
    TestLedgerDataProvider(jtx::Env* env) : env_(env)
    {
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    Expected<std::int32_t, HostFunctionError>
    getLedgerSqn() override
    {
        return env_->current()->seq();
    }
};

struct TestHostFunctions : public HostFunctions
{
    test::jtx::Env& env_;
    AccountID accountID_;
    Bytes data_;
    int clock_drift_ = 0;
    void const* rt_ = nullptr;

public:
    TestHostFunctions(test::jtx::Env& env, int cd = 0)
        : env_(env), clock_drift_(cd)
    {
        accountID_ = env_.master.id();
        std::string t = "10000";
        data_ = Bytes{t.begin(), t.end()};
    }

    virtual void
    setRT(void const* rt) override
    {
        rt_ = rt;
    }

    virtual void const*
    getRT() const override
    {
        return rt_;
    }

    beast::Journal
    getJournal() override
    {
        return env_.journal;
    }

    Expected<std::int32_t, HostFunctionError>
    getLedgerSqn() override
    {
        return 12345;
    }

};

struct TestHostFunctionsSink : public TestHostFunctions
{
    test::StreamSink sink_;
    beast::Journal jlog_;
    void const* rt_ = nullptr;

public:
    explicit TestHostFunctionsSink(test::jtx::Env& env, int cd = 0)
        : TestHostFunctions(env, cd)
        , sink_(beast::severities::kDebug)
        , jlog_(sink_)
    {
    }

    test::StreamSink&
    getSink()
    {
        return sink_;
    }

    beast::Journal
    getJournal() override
    {
        return jlog_;
    }
};


}  // namespace test
}  // namespace ripple
