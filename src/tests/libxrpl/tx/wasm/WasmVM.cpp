#include <xrpl/tx/wasm/WasmVM.h>

#include <xrpl/basics/contract.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <tx/wasm/fixtures/WasmFixture.h>
#include <tx/wasm/fixtures/WasmRun.h>

#include <array>
#include <cstdint>
#include <expected>
#include <stdexcept>
#include <string>
#include <string_view>

namespace xrpl::test {

namespace {

// One module with an export per way a run can end. Kept together because these are properties
// of the engine rather than of any host function: the only import is there so the
// out-of-gas and no-memory cases have a host call to fail in.
constexpr std::string_view kEngineWat = R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (memory (export "memory") 1)

  (func (export "escrow_finish") (result i32) (i32.const 5))

  (func (export "calls_the_host") (result i32)
    (call $ldgr_index (i32.const 0) (i32.const 4)))

  (func (export "traps") (result i32) unreachable)

  (func (export "never_returns") (result i32) (loop (br 0)) (i32.const 0))

  (func (export "wrong_signature") (param i32) (result i32) (local.get 0))

  (global (export "not_a_function") i32 (i32.const 0)))
)wat";

// The same host call with no memory exported, so the engine has nothing to resolve a byte
// region against.
constexpr std::string_view kNoMemoryWat = R"wat(
(module
  (import "host_lib" "ldgr_index" (func $ldgr_index (param i32 i32) (result i32)))
  (func (export "escrow_finish") (result i32)
    (call $ldgr_index (i32.const 0) (i32.const 4))))
)wat";

}  // namespace

class WasmVMTest : public MockVmTest
{
};

TEST_F(WasmVMTest, ContractReturnValueReachesCaller)
{
    auto const outcome = run(kEngineWat);

    ASSERT_TRUE(outcome.has_value()) << transToken(outcome.error().ter);
    EXPECT_EQ(outcome->result, 5);
    EXPECT_GT(outcome->cost, 0) << "running any instruction costs gas";
    EXPECT_LT(outcome->cost, kAmpleGas);
}

TEST_F(WasmVMTest, GuestTrapIsChargedAsContractFault)
{
    auto const outcome = run(kEngineWat, kAmpleGas, "traps");

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecFAILED_PROCESSING);
    ASSERT_TRUE(outcome.error().cost.has_value());
    EXPECT_GT(*outcome.error().cost, 0);  // NOLINT(bugprone-unchecked-optional-access)
}

TEST_F(WasmVMTest, NonTerminatingContractSpendsWholeBudget)
{
    auto const outcome = run(kEngineWat, kAmpleGas, "never_returns");

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecOUT_OF_GAS);
    ASSERT_TRUE(outcome.error().cost.has_value());

    // The cost break down is as follows:
    // 1. There is a function entry charge (finish function) which seems to be 63 units of fuel.
    // 2. Each iteration costs 2 units of fuel.
    // For a GAS amount of 100,000, we will be limited to burning an odd number of fuel.
    // So the way the test is written, the most fuel that will be used is 99,999 units.
    EXPECT_EQ(*outcome.error().cost, kAmpleGas - 1);  // NOLINT(bugprone-unchecked-optional-access)
}

// A budget too small to reach the first host charge is still out of gas, whatever the engine
// can account for by then.
TEST_F(WasmVMTest, BudgetTooSmallToRunIsOutOfGas)
{
    auto const outcome = run(kEngineWat, 1, "calls_the_host");

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecOUT_OF_GAS);
    EXPECT_TRUE(outcome.error().cost.has_value());
}

// A host call needs a memory to resolve its byte regions against, and the export is not
// optional for a contract that makes one.
TEST_F(WasmVMTest, HostCallWithNoExportedMemoryFails)
{
    auto const outcome = run(kNoMemoryWat);

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecFAILED_PROCESSING);
    EXPECT_TRUE(outcome.error().cost.has_value());
}

// A module that will not instantiate is the contract's fault and is charged, not the node's.
// Screening does not see every way this happens - a linear memory the module keeps to itself
// is absent from its exports - so such a module can pass preflight and still be refused here.
TEST_F(WasmVMTest, ModuleThatWillNotInstantiateIsChargedToTheContract)
{
    // 129 pages, not exported, so nothing outside the module declares it.
    static constexpr std::string_view wat = R"wat(
    (module
      (memory 129)
      (func (export "escrow_finish") (result i32) (i32.const 0)))
    )wat";

    EXPECT_EQ(preflightEscrowWasm(assembleWat(wat), beast::Journal{sink}), tesSUCCESS)
        << "screening cannot see an unexported memory";

    auto const outcome = run(wat);

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecFAILED_PROCESSING);
    EXPECT_TRUE(outcome.error().cost.has_value());
}

// Preflight is meant to refuse these with `temINVALID_BYTECODE`; reaching apply means the screening
// did not happen, which is the node's fault and not the transaction's.
TEST_F(WasmVMTest, UnrunnableModuleIsNodeSideFault)
{
    struct Case
    {
        char const* what;
        Bytes code;
        std::string_view entryPoint;
    };
    std::array const cases = {
        Case{
            .what = "not wasm at all", .code = Bytes{0, 1, 2, 3}, .entryPoint = escrowFunctionName},
        Case{.what = "empty", .code = Bytes{}, .entryPoint = escrowFunctionName},
        Case{
            .what = "no such export", .code = assemble(kEngineWat), .entryPoint = "no_such_export"},
        Case{
            .what = "export is not a function",
            .code = assemble(kEngineWat),
            .entryPoint = "not_a_function"},
        Case{
            .what = "export takes a parameter",
            .code = assemble(kEngineWat),
            .entryPoint = "wrong_signature"},
    };

    for (auto const& c : cases)
    {
        auto const outcome = runBytes(c.code, kAmpleGas, c.entryPoint);

        ASSERT_FALSE(outcome.has_value()) << c.what;
        EXPECT_EQ(outcome.error().ter, tecINTERNAL) << c.what;
        EXPECT_FALSE(outcome.error().cost.has_value()) << c.what;
    }
}

// wasmi's `wat` feature would make `Module::new` accept text as readily as binary, which would
// put an assembler on the consensus path and make a module's validity a build flag. The
// engine turns that feature off; this is the guest-side proof, using the very text the rest
// of this file assembles.
TEST_F(WasmVMTest, TextFormatModuleIsRejected)
{
    Bytes const text{kEngineWat.begin(), kEngineWat.end()};

    auto const outcome = runBytes(text);

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
}

// A soft host error is the contract's to interpret, so its code has to cross the boundary
// unchanged: the engine must not renumber it, clamp it, or turn it into a failure of its own.
//
// Over the whole of `HostFunctionError` rather than a sample, because `HostFunctionError` and
// the Rust ABI's `HostError` are two hand-maintained lists of the same wire numbers: -1
// through -20 have to mean the same thing on both sides, and this is the test that notices if
// either side renumbers.
//
// The two exclusions are the codes the Rust engine converts into a fault, which stops the run
// instead of reaching the guest: -1 `Unimplemented` and -14 `NoMemExported`. Both say the call
// was not served at all.
TEST_F(WasmVMTest, SoftHostErrorCodesCrossUnchanged)
{
    static constexpr HostFunctionError kSoftErrors[] = {
        HostFunctionError::FieldNotFound,
        HostFunctionError::BufferTooSmall,
        HostFunctionError::NoArray,
        HostFunctionError::NotLeafField,
        HostFunctionError::LocatorMalformed,
        HostFunctionError::SlotOutRange,
        HostFunctionError::SlotsFull,
        HostFunctionError::EmptySlot,
        HostFunctionError::LedgerObjNotFound,
        HostFunctionError::OutOfTransferLimit,
        HostFunctionError::DataFieldTooLarge,
        HostFunctionError::PointerOutOfBounds,
        HostFunctionError::InvalidParams,
        HostFunctionError::InvalidAccount,
        HostFunctionError::InvalidField,
        HostFunctionError::IndexOutOfBounds,
        HostFunctionError::FloatInputMalformed,
        HostFunctionError::FloatComputationError,
    };

    auto refused = HostFunctionError::FieldNotFound;
    EXPECT_CALL(host, getLedgerSqn())
        .WillRepeatedly([&refused]() -> std::expected<std::uint32_t, HostFunctionError> {
            return std::unexpected(refused);
        });

    for (auto const error : kSoftErrors)
    {
        refused = error;

        auto const outcome = run(kEngineWat, kAmpleGas, "calls_the_host");

        ASSERT_TRUE(outcome.has_value()) << hfErrorToInt(error) << " stopped the run";
        EXPECT_EQ(outcome->result, hfErrorToInt(error));
    }
}

// The counterpart: a fatal code stops the run rather than reaching the contract, so a host
// that cannot serve a call cannot be second-guessed by the contract.
TEST_F(WasmVMTest, FatalHostErrorStopsRun)
{
    auto refused = HostFunctionError::Unimplemented;
    EXPECT_CALL(host, getLedgerSqn())
        .WillRepeatedly([&refused]() -> std::expected<std::uint32_t, HostFunctionError> {
            return std::unexpected(refused);
        });

    for (auto const error :
         {HostFunctionError::InternalFatal,
          HostFunctionError::Unimplemented,
          HostFunctionError::NoMemExported})
    {
        refused = error;

        auto const outcome = run(kEngineWat, kAmpleGas, "calls_the_host");

        ASSERT_FALSE(outcome.has_value()) << hfErrorToInt(error) << " reached the contract";
    }
}

// The point of the bridge's C++ half: an exception must not reach the Rust frames that called
// the host, and must not take the node with it.
TEST_F(WasmVMTest, ThrowingHostFunctionBecomesInternal)
{
    EXPECT_CALL(host, getLedgerSqn())
        .WillOnce([]() -> std::expected<std::uint32_t, HostFunctionError> {
            Throw<std::runtime_error>("the ledger came apart");
        });

    auto const outcome = run(kEngineWat, kAmpleGas, "calls_the_host");

    ASSERT_FALSE(outcome.has_value());
    EXPECT_EQ(outcome.error().ter, tecINTERNAL);
    EXPECT_FALSE(outcome.error().cost.has_value()) << "a node-side fault charges nothing";
    // Caught is not swallowed: the condition has to be recorded, and the line has to name the
    // call it came out of.
    EXPECT_THAT(logged(), testing::HasSubstr("the ledger came apart"));
    EXPECT_THAT(logged(), testing::HasSubstr("getLedgerSqn"));
}

struct WasmVMDeathTest : WasmVMTest
{
};

// No gas is not a small budget, it is a malformed transaction — refused before the engine is
// asked to run anything.
TEST_F(WasmVMDeathTest, NoGasIsRefusedAsMalformedRatherThanRun)
{
    for (auto const gas : {std::int64_t{0}, std::int64_t{-1}})
    {
        EXPECT_DEBUG_DEATH(
            {
                auto const outcome = run(kEngineWat, gas);

                ASSERT_FALSE(outcome.has_value()) << "gas: " << gas;
                EXPECT_EQ(outcome.error().ter, temBAD_AMOUNT) << "gas: " << gas;
                EXPECT_FALSE(outcome.error().cost.has_value()) << "gas: " << gas;
            },
            "gas limit is positive");
    }
}

// The host caches the current ledger object, the slot table and the contract's data for the
// length of one run, so a reused one would answer a later contract out of an earlier
// contract's state.
TEST_F(WasmVMDeathTest, DirtyHostIsRefusedBeforeContractRuns)
{
    EXPECT_DEBUG_DEATH(
        {
            EXPECT_CALL(host, checkSelf()).WillOnce(testing::Return(false));
            auto const outcome = run(kEngineWat);

            ASSERT_FALSE(outcome.has_value());
            EXPECT_EQ(outcome.error().ter, tecINTERNAL);
            EXPECT_FALSE(outcome.error().cost.has_value());
            EXPECT_THAT(logged(), testing::HasSubstr("not clean"));
        },
        "host functions not clean before the run");
}

}  // namespace xrpl::test
