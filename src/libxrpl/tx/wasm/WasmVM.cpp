#include <xrpl/tx/wasm/WasmVM.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/wasm/HostContext.h>
#include <xrpl/tx/wasm/HostFunc.h>
#include <xrpl/tx/wasm/WasmCommon.h>

#include <rust/cxx.h>
#include <xrpl_wasm_vm_ffi_cxxbridge/lib.h>

#include <cstdint>
#include <expected>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace xrpl {

namespace {

using RunStatus = rs::wasm_vm::RunStatus;
using CheckStatus = rs::wasm_vm::CheckStatus;

// The engine's outcome as the caller's: a value with its gas cost, or a TER with the gas cost
// to record beside it.
//
// A `tecINTERNAL` reports no cost. It says the fault is the node's, and charging a
// transaction for a node's defect would write that defect into the ledger.
//
// Exhaustive over the status enum, with no `default`: the enum is generated from the
// engine's `RunError`, so an outcome added there fails this switch under -Wswitch -Werror
// rather than quietly picking up a neighbour's TER. The return past the switch is for the
// compilers that will not call an exhaustive switch exhaustive; it sits after the switch,
// not in a `default`, so the coverage check above still holds.
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
            return std::unexpected{WasmTER{.ter = tecOUT_OF_GAS, .cost = cost}};

        // The contract's own fault - it trapped, or it never exported the linear memory
        // its host calls need - so it is charged for what it burned reaching that point.
        case RunStatus::Trap:
        case RunStatus::NoMemory:
        // A module that will not instantiate is the contract's fault too. Screening
        // cannot see every way this happens - a linear memory the module keeps to itself
        // is absent from its exports - so a module can pass preflight and still be
        // refused here. It is a deterministic property of the code either way, and one
        // this node's own conduct had no part in.
        case RunStatus::Instantiate:
            return std::unexpected{WasmTER{.ter = tecFAILED_PROCESSING, .cost = cost}};

        // A module that will not compile, or does not expose the entry point, should have
        // been refused at preflight with `temBAD_WASM`: screening decides both from the
        // same bytes and the same engine, so agreeing here is not a matter of degree.
        // Reaching apply means the screening did not happen, which is a node-side fault
        // rather than the transaction's.
        case RunStatus::Compile:
        case RunStatus::EntryPoint:
        // The host could not serve a call, or it threw and `HostContext` caught it.
        case RunStatus::Internal:
        // The engine panicked: a defect in the engine, reported rather than fatal to the
        // node.
        case RunStatus::Panic:
            return std::unexpected{WasmTER{.ter = tecINTERNAL, .cost = std::nullopt}};
    }
    UNREACHABLE("xrpl::outcome : unknown RunStatus");
    return std::unexpected{WasmTER{.ter = tecINTERNAL, .cost = std::nullopt}};
}

// A screening verdict as a TER.
//
// `temBAD_WASM` says the transaction carries something this engine cannot run: a
// malformed transaction, refused before it can reach the ledger. A panic inside the
// engine is different in kind - nothing was learned about the module - so the answer is
// node-local rather than a claim about the transaction.
//
// Exhaustive over the status enum, with no `default`, for the same reason `outcome` is.
NotTEC
verdict(CheckStatus status)
{
    switch (status)
    {
        case CheckStatus::Ok:
            return tesSUCCESS;

        // The module will not compile, imports what no engine of this ABI serves, does
        // not export the entry point as `() -> i32`, or asks for more linear memory or
        // table than it may have.
        case CheckStatus::Compile:
        case CheckStatus::Import:
        case CheckStatus::EntryPoint:
        case CheckStatus::Memory:
        case CheckStatus::Table:
            return temBAD_WASM;

        // The engine panicked: a defect in the engine, reported rather than fatal to
        // the node, and not the transaction's fault.
        case CheckStatus::Panic:
            return telFAILED_PROCESSING;
    }
    UNREACHABLE("xrpl::verdict : unknown CheckStatus");
    return telFAILED_PROCESSING;
}

}  // namespace

std::expected<EscrowResult, WasmTER>
runEscrowWasm(
    Bytes const& wasmCode,
    HostFunctions& hfs,
    std::int64_t gasLimit,
    std::string_view funcName) noexcept
{
    // A run needs a budget to spend. Refused here rather than in the engine because what a
    // non-positive limit means is a transaction-validity rule; the engine's own budget is
    // therefore an unsigned quantity with no invalid value to represent.
    if (gasLimit <= 0)
        return std::unexpected{WasmTER{.ter = temBAD_AMOUNT, .cost = std::nullopt}};

    auto const nodeSideFault = std::unexpected{WasmTER{.ter = tecINTERNAL, .cost = std::nullopt}};

    return guarded(hfs.getJournal(), nodeSideFault, [&]() -> std::expected<EscrowResult, WasmTER> {
        // The host caches the current ledger object, the slot table and the
        // contract's data for the length of one run, so a reused one would answer a
        // later contract out of an earlier contract's state.
        XRPL_ASSERT(
            hfs.checkSelf(), "::xrpl::runEscrowWasm : host functions not clean before the run");
        if (!hfs.checkSelf())
        {
            throw std::runtime_error("host functions not clean before the run");
        }

        HostContext const ctx{hfs};
        auto const run = rs::wasm_vm::run_escrow(
            ctx,
            rust::Slice<std::uint8_t const>{wasmCode.data(), wasmCode.size()},
            static_cast<std::uint64_t>(gasLimit),
            rust::Str{funcName.data(), funcName.size()});

        auto const result = outcome(run);
        if (!result)
        {
            JLOG(hfs.getJournal().warn())
                << "wasm: " << std::string_view{run.detail.data(), run.detail.size()}
                << ", ter: " << transToken(result.error().ter);
        }
        return result;
    });
}

NotTEC
preflightEscrowWasm(Bytes const& wasmCode, beast::Journal j, std::string_view funcName) noexcept
{
    return guarded(j, NotTEC{telFAILED_PROCESSING}, [&]() {
        auto const checked = rs::wasm_vm::check_escrow(
            rust::Slice<std::uint8_t const>{wasmCode.data(), wasmCode.size()},
            rust::Str{funcName.data(), funcName.size()});

        auto const ter = verdict(checked.status);
        if (!isTesSuccess(ter))
        {
            JLOG(j.warn()) << "wasm: "
                           << std::string_view{checked.detail.data(), checked.detail.size()}
                           << ", ter: " << transToken(ter);
        }
        return ter;
    });
}

}  // namespace xrpl
