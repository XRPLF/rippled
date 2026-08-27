#include <test/app/invariants/InvariantsBase.h>

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/fee.h>
#include <test/jtx/tags.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/applySteps.h>

#include <chrono>
#include <initializer_list>
#include <memory>
#include <source_location>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

test::jtx::Env
InvariantsBase::makeEnv(FeatureBitset features)
{
    return {*this, test::jtx::envconfig(), features, nullptr, beast::Severity::Disabled};
}

void
InvariantsBase::doInvariantCheck(
    std::vector<std::string> const& expectLogs,
    Precheck const& precheck,
    XRPAmount fee,
    STTx tx,
    std::initializer_list<TER> ters,
    Preclose const& preclose,
    TxAccount setTxAccount,
    std::source_location const& loc,
    TER initialResult)
{
    doInvariantCheck(
        makeEnv(test::jtx::testableAmendments()),
        expectLogs,
        precheck,
        fee,
        tx,
        ters,
        preclose,
        setTxAccount,
        loc,
        initialResult);
}

void
InvariantsBase::doInvariantCheck(
    test::jtx::Env&& env,
    std::vector<std::string> const& expectLogs,
    Precheck const& precheck,
    XRPAmount fee,
    STTx tx,
    std::initializer_list<TER> ters,
    Preclose const& preclose,
    TxAccount setTxAccount,
    std::source_location const& loc,
    TER initialResult)
{
    using namespace test::jtx;

    Account const a1{"A1"};
    Account const a2{"A2"};
    env.fund(XRP(1000), a1, a2);
    if (preclose)
        BEAST_EXPECT(preclose(a1, a2, env));
    env.close();

    if (setTxAccount != TxAccount::None)
        tx.setAccountID(sfAccount, setTxAccount == TxAccount::A1 ? a1.id() : a2.id());

    doInvariantCheck(
        std::move(env), a1, a2, expectLogs, precheck, fee, tx, ters, loc, initialResult);
}

void
InvariantsBase::doInvariantCheck(
    // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
    test::jtx::Env&& env,
    test::jtx::Account const& a1,
    test::jtx::Account const& a2,
    std::vector<std::string> const& expectLogs,
    Precheck const& precheck,
    XRPAmount fee,
    STTx tx,
    std::initializer_list<TER> ters,
    std::source_location const& loc,
    TER initialResult)
{
    using namespace test::jtx;

    OpenView ov{*env.current()};
    test::StreamSink sink{beast::Severity::Warning};
    beast::Journal const jlog{sink};
    ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};

    // Invariants normally run in the Transaction's "apply" (operator()) context, and can always
    // access global Rules.
    CurrentTransactionRulesGuard const rulesGuard(ov.rules());

    BEAST_EXPECT(precheck(a1, a2, ac));

    auto transactor = makeTransactor(ac);
    if (!BEAST_EXPECT(transactor))
        return;

    // Invoke the check twice to cover the tec and tef cases. Both passes run
    // against the same view -- production would discard it in between -- so
    // the second sees the same violation and escalates tec -> tef. A
    // {tec, tef} pair therefore means "enforced whatever the incoming
    // result", not that the transaction ends in tef on ledger.
    if (!BEAST_EXPECT(ters.size() == 2))
        return;

    TER terActual = initialResult;
    for (TER const& terExpect : ters)
    {
        TER const terInput = terActual;
        terActual = transactor->checkInvariants(terActual, fee, Transactor::InvariantScope::Full);
        expect(
            terExpect == terActual,
            "expected: " + transToken(terExpect) + " got: " + transToken(terActual),
            loc.file_name(),
            loc.line());
        auto const messages = sink.messages().str();

        // checkInvariants returns its input unchanged unless something
        // fires, so a changed result means an invariant fired, and a firing
        // invariant must log.
        if (terActual != terInput)
        {
            expect(
                messages.starts_with("Invariant failed:") ||
                    messages.starts_with("Transaction caused an exception"),
                messages,
                loc.file_name(),
                loc.line());
        }

        // std::cerr << messages << '\n';
        for (auto const& m : expectLogs)
        {
            expect(messages.contains(m), m, loc.file_name(), loc.line());
        }
    }
}

Keylet
InvariantsBase::createLoanBroker(
    jtx::Account const& a,
    jtx::Env& env,
    jtx::PrettyAsset const& asset)
{
    using namespace jtx;

    // Under featureLendingProtocolV1_1 LoanBrokerSet::preclaim only
    // accepts closed-ended vaults. Build one with a comfortable
    // subscription window; LoanBrokerSet itself is not phase-gated,
    // so leaving the vault in the Subscription phase is fine here.
    uint256 vaultID;
    Vault const vault{env};
    auto [tx, vKeylet, _] = vault.createClosedEnded(
        {.owner = a,
         .asset = asset,
         .subscriptionOffset = std::chrono::seconds{60},
         .investmentWindow = std::chrono::seconds{kMinInvestmentPeriod + 1'000'000u}});
    env(tx);
    BEAST_EXPECT(env.le(vKeylet));

    vaultID = vKeylet.key;

    // Create Loan Broker
    using namespace loan_broker;

    auto const loanBrokerKeylet = keylet::loanBroker(a.id(), SeqProxy::rawSequence(env.seq(a)));
    // Create a Loan Broker with all default values.
    env(set(a, vaultID), Fee(kIncrement));

    return loanBrokerKeylet;
}

}  // namespace xrpl::test
