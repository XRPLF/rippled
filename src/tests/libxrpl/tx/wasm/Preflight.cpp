#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>
#include <xrpl/tx/wasm/WasmVM.h>

#include <gtest/gtest.h>
#include <tx/wasm/WasmFixture.h>

#include <string>
#include <string_view>

namespace xrpl::test {

namespace {

// A contract the engine can run: it compiles, imports only a declared host function, and
// exports the entry point as `() -> i32`.
constexpr std::string_view kRunnableWat = R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)
  (func (export "escrow_finish") (result i32)
    (call $ldgr_index (i32.const 0) (i32.const 4))))
)wat";

}  // namespace

// `preflightEscrowWasm` takes no host, so this fixture holds none - which is the point of
// the signature, and what deriving from `WasmTest` would hide. Only a journal, to read the
// refusal out of.
struct PreflightTest : testing::Test
{
    CaptureSink sink{beast::Severity::Warning};

    NotTEC
    preflight(std::string_view wat, std::string_view funcName = escrowFunctionName)
    {
        return preflightEscrowWasm(assembleWat(wat), beast::Journal{sink}, funcName);
    }

    NotTEC
    preflightBytes(Bytes const& wasm, std::string_view funcName = escrowFunctionName)
    {
        return preflightEscrowWasm(wasm, beast::Journal{sink}, funcName);
    }

    [[nodiscard]] std::string
    logged() const
    {
        return sink.messages();
    }
};

TEST_F(PreflightTest, RunnableContractPasses)
{
    EXPECT_EQ(preflight(kRunnableWat), tesSUCCESS);
    EXPECT_TRUE(logged().empty()) << logged();
}

TEST_F(PreflightTest, GarbageIsRefused)
{
    EXPECT_EQ(preflightBytes(Bytes{}), temBAD_WASM);
    EXPECT_EQ(preflightBytes(Bytes{0x00, 0x61, 0x73, 0x6d}), temBAD_WASM);
}

// The engine takes wasm binaries, and text is not one. The suite writes its modules as text
// and assembles them, so this feeds the engine the very text the other tests assemble: a
// transaction's validity must not depend on whether an assembler was linked in.
TEST_F(PreflightTest, TextFormatModuleIsRefused)
{
    Bytes const text{kRunnableWat.begin(), kRunnableWat.end()};

    EXPECT_EQ(preflightBytes(text), temBAD_WASM);
    EXPECT_EQ(preflight(kRunnableWat), tesSUCCESS) << "the same module, assembled first";
}

TEST_F(PreflightTest, ImportOfAnUnknownHostFunctionIsRefused)
{
    constexpr std::string_view wat = R"wat(
    (module
      (import "host_lib" "no_such_function" (func $f (param i32) (result i32)))
      (memory (export "memory") 1)
      (func (export "escrow_finish") (result i32) (call $f (i32.const 0))))
    )wat";

    EXPECT_EQ(preflight(wat), temBAD_WASM);
    EXPECT_THAT(logged(), testing::HasSubstr("no host function 'no_such_function'"));
}

// Host functions are registered under one module name. `env` is what plain clang emits, so a
// contract built without the SDK's import attributes lands here.
TEST_F(PreflightTest, ImportFromAnotherModuleIsRefused)
{
    constexpr std::string_view wat = R"wat(
    (module
      (import "env" "ldgr_index" (func $f (param i32 i32) (result i32)))
      (memory (export "memory") 1)
      (func (export "escrow_finish") (result i32) (i32.const 0)))
    )wat";

    EXPECT_EQ(preflight(wat), temBAD_WASM);
    EXPECT_THAT(logged(), testing::HasSubstr("is not from 'host_lib'"));
}

// A contract asking for more linear memory than the engine grants can never run, so it is
// refused before it can be escrowed. The cap itself is granted.
TEST_F(PreflightTest, MemoryPastTheCapIsRefused)
{
    constexpr std::string_view tooMuch = R"wat(
    (module
      (memory (export "memory") 129)
      (func (export "escrow_finish") (result i32) (i32.const 0)))
    )wat";

    EXPECT_EQ(preflight(tooMuch), temBAD_WASM);
    EXPECT_THAT(logged(), testing::HasSubstr("memory: initial memory of 129 pages"));

    constexpr std::string_view atTheCap = R"wat(
    (module
      (memory (export "memory") 128)
      (func (export "escrow_finish") (result i32) (i32.const 0)))
    )wat";

    EXPECT_EQ(preflight(atTheCap), tesSUCCESS);
}

TEST_F(PreflightTest, MissingEntryPointIsRefused)
{
    constexpr std::string_view wat = R"wat(
    (module
      (memory (export "memory") 1)
      (func (export "other") (result i32) (i32.const 0)))
    )wat";

    EXPECT_EQ(preflight(wat), temBAD_WASM);
    EXPECT_THAT(logged(), testing::HasSubstr("no entry point 'escrow_finish'"));
}

TEST_F(PreflightTest, EntryPointOfTheWrongTypeIsRefused)
{
    constexpr std::string_view wat = R"wat(
    (module
      (memory (export "memory") 1)
      (func (export "escrow_finish") (result i64) (i64.const 0)))
    )wat";

    EXPECT_EQ(preflight(wat), temBAD_WASM);
    EXPECT_THAT(logged(), testing::HasSubstr("has the wrong signature"));
}

// Screening is for the entry point the caller names, as a run is: a contract screened for one
// export says nothing about another.
TEST_F(PreflightTest, EntryPointIsTheNameTheCallerGives)
{
    constexpr std::string_view wat = R"wat(
    (module
      (memory (export "memory") 1)
      (func (export "other") (result i32) (i32.const 0)))
    )wat";

    EXPECT_EQ(preflight(wat, "other"), tesSUCCESS);
    EXPECT_EQ(preflight(wat), temBAD_WASM);
}

// Every refusal is logged with the engine's own description and the TER: without it a node
// operator has a `temBAD_WASM` and no way to tell a contract author which of the three
// stages refused the module.
TEST_F(PreflightTest, RefusalNamesTheReasonAndTheTer)
{
    EXPECT_EQ(preflightBytes(Bytes{0x00, 0x61, 0x73, 0x6d}), temBAD_WASM);

    EXPECT_THAT(logged(), testing::HasSubstr("compile: "));
    EXPECT_THAT(logged(), testing::HasSubstr(transToken(temBAD_WASM)));
}

// A module that passes screening still has to pass the run's own stages, and one that fails
// screening would have failed the run. Same modules through both entry points, so the two do
// not have to be trusted to agree.
TEST_F(PreflightTest, ScreeningAgreesWithARun)
{
    struct Case
    {
        std::string_view label;
        std::string_view wat;
        bool passes;
    };

    // clang-format off
    constexpr Case cases[]{
        {.label = "a runnable contract", .wat = kRunnableWat, .passes = true},
        {.label = "an unknown host function",
         .wat = R"wat((module (import "host_lib" "nope" (func $f (result i32)))
                        (memory (export "memory") 1)
                        (func (export "escrow_finish") (result i32) (call $f))))wat",
         .passes = false},
        {.label = "no entry point",
         .wat = R"wat((module (memory (export "memory") 1)
                        (func (export "other") (result i32) (i32.const 0))))wat",
         .passes = false},
    };
    // clang-format on

    for (auto const& [label, wat, passes] : cases)
    {
        auto const screened = preflight(wat);
        EXPECT_EQ(isTesSuccess(screened), passes) << label;

        // The run's own verdict on the same bytes. A refused module must not reach the
        // contract's first instruction; an accepted one must get past the entry-point
        // lookup, whatever it then does.
        testing::StrictMock<MockHostFunctions> host{beast::Journal{sink}};
        EXPECT_CALL(host, checkSelf()).WillRepeatedly(testing::Return(true));
        EXPECT_CALL(host, getLedgerSqn()).WillRepeatedly(testing::Return(7u));

        auto const ran = runEscrowWasm(assembleWat(wat), host, 100'000);
        EXPECT_EQ(ran.has_value(), passes) << label;
    }
}

}  // namespace xrpl::test
