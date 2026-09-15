#pragma once

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STData.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/wasm/ContractContext.h>
#include <xrpl/tx/wasm/ContractHostFuncImpl.h>

#include <helpers/Account.h>
#include <tx/wasm/fixtures/WasmLedger.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// A real ledger with a real contract host over it, and **no test framework**, for the same
// reason `WasmLedger` has none: a benchmark wants a ledger and a host, not GTest's lifecycle.
// `ContractHostFixture` adds the framework and the assertions on top.
//
// Setup steps here throw (`fixtureFailed`) rather than `EXPECT_`. See `WasmLedger.h`.

namespace xrpl::test {

// The pieces a `ContractHostFunctionsImpl` borrows, kept alive together and in the order the
// host needs them: the transaction the context reads, the context the contract context holds,
// and the contract context the host holds.
class ContractHost
{
public:
    ContractHost(
        std::shared_ptr<STTx const> tx,
        std::unique_ptr<ApplyContext> context,
        std::unique_ptr<ContractContext> contractContext,
        std::unique_ptr<ContractHostFunctionsImpl> host);

    ContractHostFunctionsImpl*
    operator->() const;
    ContractHostFunctionsImpl&
    operator*() const;

    // What the contract has accumulated so far: its data cache, its event map, the
    // transactions it built and the ones it emitted.
    ContractContext&
    context() const;

    ApplyContext&
    applyContext() const;

    // Write the contract's data cache and events into the ledger, as `ContractCall::doApply`
    // does once the contract returns, then apply the transaction. The data objects are
    // readable through `ContractLedger::contractData` afterwards.
    TER
    finalize() const;

private:
    std::shared_ptr<STTx const> tx_;
    std::unique_ptr<ApplyContext> context_;
    std::unique_ptr<ContractContext> contractContext_;
    std::unique_ptr<ContractHostFunctionsImpl> host_;
};

// How a contract host is to be built. Everything has a default that a test does not have to
// think about; a test names only what it is about.
struct ContractHostOptions
{
    // The contract's own account, which owns nothing a test has to fund unless the contract
    // pays out.
    AccountID contractAccount;

    // The account whose transaction is running, which is what `otxnAccount` reports.
    AccountID caller;

    // The contract instance's parameters, as `ContractCall::doApply` reads them off the
    // Contract ledger entry.
    std::vector<ParameterValueVec> instanceParameters{};

    // The parameters this call was given.
    std::vector<ParameterValueVec> functionParameters{};

    // The contract instance, which is also the key the host's object cache is rooted at.
    std::optional<Keylet> contractKeylet{};

    uint256 contractHash{1};
};

class ContractLedger : public WasmLedger
{
public:
    // A host over a `ttCONTRACT_CALL`, built the way `ContractCall::doApply` builds one.
    ContractHost
    makeContractHost(ContractHostOptions options);

    // The same, with `trace` output captured; read it back with `logged()`.
    ContractHost
    makeTracingContractHost(ContractHostOptions options);

    // The data object `owner` keeps under `contractAccount`, or nullptr if the contract has
    // never written one.
    [[nodiscard]] std::shared_ptr<SLE const>
    contractData(AccountID const& owner, AccountID const& contractAccount);

private:
    ContractHost
    makeContractHost(beast::Journal journal, ContractHostOptions options);
};

// -------------------------------------------------------------------------------------
// Parameter builders, mirroring what `src/test/jtx/contract.h` puts in a transaction.
// -------------------------------------------------------------------------------------

// A parameter value, as the Contract ledger entry and the ContractCall transaction carry it.
template <class T>
ParameterValueVec
param(T const& value)
{
    return ParameterValueVec{STData{sfParameterValue, value}};
}

}  // namespace xrpl::test
