#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/MockHostFunctions.h>
#include <xrpl_wasm_testkit_cxxbridge/lib.h>

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace xrpl::test {

// Keeps what a run logged. The host's default journal is a null sink, which would let a
// swallowed condition pass a test that only checks the TER.
class CapturingSink : public beast::Journal::Sink
{
    std::string text_;

public:
    CapturingSink() : Sink(beast::Severity::Warning, false)
    {
    }

    void
    write(beast::Severity level, std::string const& text) override
    {
        writeAlways(level, text);
    }

    void
    writeAlways(beast::Severity, std::string const& text) override
    {
        text_ += text;
        text_ += '\n';
    }

    [[nodiscard]] std::string const&
    text() const
    {
        return text_;
    }
};

// Assemble `wat`. Throws `rust::Error` on a typo, which gtest reports against the test that
// holds it.
//
// A free function because not every wasm test needs a host: `preflightEscrowWasm` takes none,
// so its fixture derives from `testing::Test` rather than from `WasmTest`.
inline Bytes
assembleWat(std::string_view wat)
{
    auto const wasm = rs::wasm_testkit::compile_wat(rust::Str(wat.data(), wat.size()));
    return Bytes{wasm.begin(), wasm.end()};
}

// Base for every wasm test that runs a contract: a mocked host whose log is captured, and one
// way into the engine.
//
// Modules are written as WebAssembly text and assembled by `assembleWat`. The assembler is in
// a test-only crate: the engine itself refuses text
// (`the_vm_refuses_a_text_format_module`), because a text assembler on the consensus path
// would make a transaction's validity a build flag.
class WasmTest : public testing::Test
{
protected:
    // Enough for every module here to run to completion; a test about budgets passes its own.
    static constexpr std::int64_t kAmpleGas = 100'000;

    CapturingSink sink_;

    // Strict: a host call no test asked for is a failure, not a warning. These modules import
    // exactly what they mean to exercise, so an unplanned call means the engine reached for
    // something on its own — which is the kind of surprise a test suite exists to catch.
    testing::StrictMock<MockHostFunctions> host_{beast::Journal{sink_}};

    WasmTest()
    {
        // `runEscrowWasm` asks every run whether the host is clean, so under a strict mock
        // every test would have to say so. Declared once here, and any number of times
        // (including none, for the runs refused before the engine is reached). A test that
        // cares says otherwise and its own expectation wins.
        EXPECT_CALL(host_, checkSelf()).WillRepeatedly(testing::Return(true));
    }

    static Bytes
    assemble(std::string_view wat)
    {
        return assembleWat(wat);
    }

    std::expected<EscrowResult, WasmTER>
    run(std::string_view wat,
        std::int64_t gas = kAmpleGas,
        std::string_view entryPoint = escrowFunctionName)
    {
        return runEscrowWasm(assemble(wat), host_, gas, entryPoint);
    }

    std::expected<EscrowResult, WasmTER>
    runBytes(
        Bytes const& wasm,
        std::int64_t gas = kAmpleGas,
        std::string_view entryPoint = escrowFunctionName)
    {
        return runEscrowWasm(wasm, host_, gas, entryPoint);
    }

    [[nodiscard]] std::string const&
    logged() const
    {
        return sink_.text();
    }
};

// Base for the per-host-function fixtures. Each derives, supplies the module that exercises
// its own import, and runs it through `callHost()` — so a test says only what the host was
// asked and what came back.
class HostCallTest : public WasmTest
{
protected:
    // The module under test. One import, one `escrow_finish` that calls it.
    [[nodiscard]] virtual std::string
    wat() const = 0;

    std::expected<EscrowResult, WasmTER>
    callHost(std::string_view entryPoint = escrowFunctionName)
    {
        return run(wat(), kAmpleGas, entryPoint);
    }

    // The contract's return value, which for these modules is what the host answered — or
    // its negative error code. Fails the test if the run did not complete.
    std::int32_t
    hostAnswer(std::string_view entryPoint = escrowFunctionName)
    {
        auto const outcome = callHost(entryPoint);
        if (!outcome)
        {
            ADD_FAILURE() << "the run did not complete: " << transToken(outcome.error().ter)
                          << "; logged: " << logged();
            return 0;
        }
        return outcome->result;
    }
};

}  // namespace xrpl::test
