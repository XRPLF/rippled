#include <xrpl/tx/wasm/WasmVM.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostContext.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <xrpl_wasm_vm_ffi_cxxbridge/lib.h>

#include <cstdint>
#include <exception>
#include <expected>
#include <optional>
#include <string_view>

namespace xrpl {

namespace {

using RunStatus = rs::wasm_vm::RunStatus;

// The engine's outcome as the caller's: a value with its cost, or a TER with the cost to
// record beside it.
//
// A `tecINTERNAL` reports no cost. It says the fault is the node's, and charging a
// transaction for a node's defect would write that defect into the ledger.
//
// Exhaustive over the status enum, with no `default`: the enum is generated from the
// engine's `RunError`, so an outcome added there fails this switch under -Wswitch -Werror
// rather than quietly picking up a neighbour's TER.
std::expected<EscrowResult, WasmTER>
outcome(rs::wasm_vm::RunResult const& run)
{
    auto const cost = static_cast<std::int64_t>(run.gas_used);

    switch (run.status)
    {
        case RunStatus::Ok:
            return EscrowResult{.result = run.result, .cost = cost};

        // The cost is the whole limit: XLS-0102 halts the guest the instant the meter runs
        // out, and the run is charged for all of it.
        case RunStatus::OutOfGas:
            return std::unexpected(WasmTER{.ter = tecOUT_OF_GAS, .cost = cost});

        // The contract's own fault - it trapped, or it never exported the linear memory
        // its host calls need - so it is charged for what it burned reaching that point.
        case RunStatus::Trap:
        case RunStatus::NoMemory:
            return std::unexpected(WasmTER{.ter = tecFAILED_PROCESSING, .cost = cost});

        // A module that will not compile, instantiate, or expose the entry point should
        // have been refused at preflight with `temBAD_WASM`. Reaching apply means the
        // screening did not happen, which is a node-side fault rather than the
        // transaction's.
        case RunStatus::Compile:
        case RunStatus::Instantiate:
        case RunStatus::EntryPoint:
        // The host could not serve a call, or it threw and `HostContext` caught it.
        case RunStatus::Internal:
        // The engine panicked: a defect in the engine, reported rather than fatal to the
        // node.
        case RunStatus::Panic:
            return std::unexpected(WasmTER{.ter = tecINTERNAL, .cost = std::nullopt});
    }

    // Not reachable through the enum, but a value outside it is representable.
    return std::unexpected(WasmTER{.ter = tecINTERNAL, .cost = std::nullopt});
}

}  // namespace

std::expected<EscrowResult, WasmTER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::int64_t gasLimit,
    std::string_view funcName)
{
    // A run needs a budget to spend. Refused here rather than in the engine because what a
    // non-positive limit means is a transaction-validity rule; the engine's own budget is
    // therefore an unsigned quantity with no invalid value to represent.
    if (gasLimit <= 0)
        return std::unexpected(WasmTER{.ter = temBAD_AMOUNT, .cost = std::nullopt});

    try
    {
        // The host caches the current ledger object, the slot table and the contract's
        // data for the length of one run, so a reused one would answer a later contract
        // out of an earlier contract's state.
        if (!hfs.checkSelf())
        {
            JLOG(hfs.getJournal().error()) << "wasm: host functions not clean before the run";
            return std::unexpected(WasmTER{.ter = tecINTERNAL, .cost = std::nullopt});
        }

        HostContext ctx{hfs};
        auto const run = rs::wasm_vm::run_escrow(
            ctx,
            rust::Slice<std::uint8_t const>(wasmCode.data(), wasmCode.size()),
            static_cast<std::uint64_t>(gasLimit),
            rust::Str(funcName.data(), funcName.size()));

        auto const result = outcome(run);
        if (!result)
        {
            JLOG(hfs.getJournal().warn())
                << "wasm: " << std::string_view(run.detail.data(), run.detail.size())
                << ", ter: " << transToken(result.error().ter);
        }
        return result;
    }
    // The engine reports every wasm outcome as a status rather than an exception, so
    // anything caught here is xrpld's own: a bad allocation, or a `funcName` that is not
    // valid UTF-8 and so cannot become a `rust::Str`.
    catch (std::exception const& e)
    {
        JLOG(hfs.getJournal().error()) << "wasm: engine call threw: " << e.what();
    }
    catch (...)
    {
        JLOG(hfs.getJournal().error()) << "wasm: engine call threw a non-exception";
    }

    return std::unexpected(WasmTER{.ter = tecINTERNAL, .cost = std::nullopt});
}

}  // namespace xrpl
