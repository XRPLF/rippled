#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>

#include <xrpld/app/main/Application.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <string>
#include <vector>

namespace xrpl {

class Transactor;

// Test-only factory — not part of the public API.
// The returned Transactor holds a raw reference to ctx; the caller must ensure
// the ApplyContext outlives the Transactor. Implemented in applySteps.cpp
std::unique_ptr<Transactor>
makeTransactor(ApplyContext& ctx);

}  // namespace xrpl

namespace xrpl::test {

class InvariantsBase : public beast::unit_test::Suite
{
protected:
    // The optional Preclose function is used to process additional transactions
    // on the ledger after creating two accounts, but before closing it, and
    // before the Precheck function. These should only be valid functions, and
    // not direct manipulations. Preclose is not commonly used.
    using Preclose = std::function<
        bool(test::jtx::Account const& a, test::jtx::Account const& b, test::jtx::Env& env)>;

    // this is common setup/method for running a failing invariant check. The
    // precheck function is used to manipulate the ApplyContext with view
    // changes that will cause the check to fail.
    using Precheck = std::function<
        bool(test::jtx::Account const& a, test::jtx::Account const& b, ApplyContext& ac)>;

    enum class TxAccount : int { None = 0, A1, A2 };

    test::jtx::Env
    makeEnv(FeatureBitset features);

    /**
     * Run a specific test case to put the ledger into a state that will be
     * detected by an invariant. Simulates the actions of a transaction that
     * would violate an invariant.
     *
     * @param expectLogs One or more messages related to the failing invariant
     *  that should be in the log output
     * @param precheck See "Precheck" above
     * @param fee If provided, the fee amount paid by the simulated transaction.
     * @param tx A mock transaction that took the actions to trigger the
     *  invariant. In most cases, only the type matters.
     * @param ters The TER results expected on the two passes of the invariant
     *  checker.
     * @param preclose See "Preclose" above. Note that @preclose runs *before*
     *  @precheck, but is the last parameter for historical reasons
     * @param setTxAccount optionally set to add sfAccount to tx (either A1 or A2)
     */
    void
    doInvariantCheck(
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        Preclose const& preclose = {},
        TxAccount setTxAccount = TxAccount::None,
        std::source_location const& loc = std::source_location::current(),
        // Result fed to the invariant checker on the first pass. Set it to a
        // tec to exercise result-dependent invariants; the harness runs no
        // transactor, so one never arises on its own.
        TER initialResult = tesSUCCESS);

    void
    doInvariantCheck(
        test::jtx::Env&& env,
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        Preclose const& preclose = {},
        TxAccount setTxAccount = TxAccount::None,
        std::source_location const& loc = std::source_location::current(),
        TER initialResult = tesSUCCESS);

    void
    doInvariantCheck(
        // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
        test::jtx::Env&& env,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::vector<std::string> const& expectLogs,
        Precheck const& precheck,
        XRPAmount fee = XRPAmount{},
        STTx tx = STTx{ttACCOUNT_SET, [](STObject&) {}},
        std::initializer_list<TER> ters = {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
        std::source_location const& loc = std::source_location::current(),
        TER initialResult = tesSUCCESS);

    Keylet
    createLoanBroker(jtx::Account const& a, jtx::Env& env, jtx::PrettyAsset const& asset);

    // Build an ltLOAN SLE with every SoeRequired field explicitly set and
    // every SoeDefault field the invariants read via `at()` materialized, so
    // rawInsert-based tests don't accidentally trip an unrelated invariant
    // or throw from a missing SoeDefault field.
    static SLE::pointer
    makeLoanSle(uint256 const& loanBrokerID, std::uint32_t loanSeq, AccountID const& borrower);
};

}  // namespace xrpl::test
