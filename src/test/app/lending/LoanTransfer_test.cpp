#include <test/app/lending/LoanTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/helpers/LendingHelpers.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace xrpl::test {

namespace {

// The terms of the loan a LoanTransfer test originates.
struct LoanTerms
{
    STAmount principal = jtx::XRP(500).value();
    std::uint32_t interestRate = 5'000;
    std::uint32_t paymentTotal = 12;
    std::uint32_t paymentInterval = 30 * 24 * 60 * 60;
};

}  // namespace

class LoanTransfer_test : public LoanTestBase
{
    // Two XRP vaults, each with a LoanBroker, and one Loan on the first broker.
    struct Market
    {
        Keylet vaultA;
        Keylet vaultB;
        Keylet brokerA;
        Keylet brokerB;
        Keylet loan;
    };

    jtx::Account const ownerA_{"ownerA"};
    jtx::Account const ownerB_{"ownerB"};
    jtx::Account const depositor_{"depositor"};
    jtx::Account const borrower_{"borrower"};

    void
    fundAccounts(jtx::Env& env)
    {
        using namespace jtx;
        env.fund(XRP(1'000'000), ownerA_, ownerB_, depositor_, borrower_);
        env.close();
    }

    static Keylet
    makeVault(
        jtx::Env& env,
        jtx::Account const& owner,
        jtx::Account const& depositor,
        STAmount const& deposit)
    {
        using namespace jtx;
        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = deposit.asset()});
        env(tx);
        env.close();
        if (deposit != beast::kZero)
            env(vault.deposit({.depositor = depositor, .id = keylet.key, .amount = deposit}));
        env.close();
        return keylet;
    }

    template <class... Fn>
    static Keylet
    makeBroker(jtx::Env& env, jtx::Account const& owner, Keylet const& vault, Fn&&... fn)
    {
        using namespace jtx;
        auto const keylet = keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, vault.key), std::forward<Fn>(fn)...);
        env.close();
        return keylet;
    }

    static Keylet
    makeLoan(
        jtx::Env& env,
        jtx::Account const& owner,
        jtx::Account const& borrower,
        Keylet const& broker,
        LoanTerms const& terms)
    {
        using namespace jtx;
        std::uint32_t const sequence = env.le(broker)->at(sfLoanSequence);
        env(loan::set(borrower, broker.key, terms.principal),
            loan::kInterestRate(TenthBips32(terms.interestRate)),
            loan::kGracePeriod(60),
            loan::kPaymentInterval(terms.paymentInterval),
            loan::kPaymentTotal(terms.paymentTotal),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();
        return keylet::loan(broker.key, SeqProxy::rawSequence(sequence));
    }

    Market
    makeMarket(jtx::Env& env, jtx::Account const& ownerB, LoanTerms const& terms = {})
    {
        using namespace jtx;
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerB, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(env, ownerB, vaultB);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, terms);
        return {vaultA, vaultB, brokerA, brokerB, loan};
    }

    static Number
    exposureOf(jtx::Env& env, Market const& m)
    {
        return loanVaultExposure(env.le(m.vaultA), env.le(m.loan));
    }

    static bool
    inDirectory(jtx::Env& env, AccountID const& owner, SLE const& entry, SF_UINT64 const& node)
    {
        auto const page = env.le(keylet::page(keylet::ownerDir(owner), entry.at(node)));
        if (!page)
            return false;
        for (auto const& index : page->getFieldV256(sfIndexes))
        {
            if (index == entry.key())
                return true;
        }
        return false;
    }

    // A transfer from broker A to broker B submitted by ownerA.
    [[nodiscard]] json::Value
    transferTx(Market const& m) const
    {
        return jtx::loan::transfer(ownerA_, m.loan.key, m.brokerB.key);
    }

    void
    testDisabled()
    {
        testcase("disabled");
        using namespace jtx;

        Env env(*this, all_ - featureStructuredCredit);
        fundAccounts(env);

        env(loan::transfer(ownerA_, uint256{1}, uint256{2}), Ter(temDISABLED));
    }

    void
    testTransferSameOwner()
    {
        testcase("transfer between brokers of one owner");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);

        auto const exposure = exposureOf(env, m);
        auto const vaultA = env.le(m.vaultA);
        auto const vaultB = env.le(m.vaultB);
        auto const brokerA = env.le(m.brokerA);
        auto const brokerB = env.le(m.brokerB);
        auto const loanBefore = env.le(m.loan);
        auto const borrowerNode = loanBefore->at(sfOwnerNode);

        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key));
        env.close();

        auto const loan = env.le(m.loan);
        if (!BEAST_EXPECT(loan))
            return;
        BEAST_EXPECT(loan->at(sfLoanBrokerID) == m.brokerB.key);
        BEAST_EXPECT(loan->at(sfOwnerNode) == borrowerNode);
        BEAST_EXPECT(inDirectory(env, env.le(m.brokerB)->at(sfAccount), *loan, sfLoanBrokerNode));
        BEAST_EXPECT(!inDirectory(env, brokerA->at(sfAccount), *loanBefore, sfLoanBrokerNode));
        BEAST_EXPECT(inDirectory(env, borrower_.id(), *loan, sfOwnerNode));

        BEAST_EXPECT(env.le(m.vaultA)->at(sfAssetsTotal) == vaultA->at(sfAssetsTotal));
        BEAST_EXPECT(env.le(m.vaultB)->at(sfAssetsTotal) == vaultB->at(sfAssetsTotal));
        BEAST_EXPECT(
            env.le(m.vaultA)->at(sfAssetsAvailable) == vaultA->at(sfAssetsAvailable) + exposure);
        BEAST_EXPECT(
            env.le(m.vaultB)->at(sfAssetsAvailable) == vaultB->at(sfAssetsAvailable) - exposure);
        BEAST_EXPECT(env.le(m.brokerA)->at(sfDebtTotal) == brokerA->at(sfDebtTotal) - exposure);
        BEAST_EXPECT(env.le(m.brokerB)->at(sfDebtTotal) == brokerB->at(sfDebtTotal) + exposure);
        BEAST_EXPECT(env.le(m.brokerA)->at(sfOwnerCount) == brokerA->at(sfOwnerCount) - 1);
        BEAST_EXPECT(env.le(m.brokerB)->at(sfOwnerCount) == brokerB->at(sfOwnerCount) + 1);
    }

    void
    testTransferCashBasis()
    {
        testcase("transfer between cash-basis vaults");
        using namespace jtx;

        Env env(*this, all_ | featureLendingProtocolV1_1);
        fundAccounts(env);

        Vault const vault{env};
        auto [txA, vaultA, subA] = vault.createClosedEnded({.owner = ownerA_, .asset = xrpIssue()});
        env(txA);
        auto [txB, vaultB, subB] = vault.createClosedEnded({.owner = ownerB_, .asset = xrpIssue()});
        env(txB);
        env.close();
        env(vault.deposit({.depositor = depositor_, .id = vaultA.key, .amount = XRP(10'000)}));
        env(vault.deposit({.depositor = depositor_, .id = vaultB.key, .amount = XRP(10'000)}));
        env.close();
        vault.closePastSubscription(std::max(subA, subB));

        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(env, ownerB_, vaultB);
        // A closed-ended vault's loans must end inside its investment window.
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {.paymentInterval = 3'600});
        auto const sleLoan = env.le(loan);
        if (!BEAST_EXPECT(sleLoan))
            return;
        auto const principal = sleLoan->at(sfPrincipalOutstanding);
        BEAST_EXPECT(loanVaultExposure(env.le(vaultA), env.le(loan)) == principal);
        auto const availableB = env.le(vaultB)->at(sfAssetsAvailable);

        env(loan::transfer(ownerA_, loan.key, brokerB.key),
            Sig(sfCounterpartySignature, ownerB_),
            Fee(env.current()->fees().base * 2));
        env.close();
        BEAST_EXPECT(env.le(loan)->at(sfLoanBrokerID) == brokerB.key);
        BEAST_EXPECT(env.le(vaultB)->at(sfAssetsAvailable) == availableB - principal);
    }

    void
    testTransferCounterparty()
    {
        testcase("transfer co-signed by the other owner");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerB_);

        env(loan::transfer(ownerB_, m.loan.key, m.brokerB.key),
            Sig(sfCounterpartySignature, ownerA_),
            Fee(env.current()->fees().base * 2));
        env.close();
        BEAST_EXPECT(env.le(m.loan)->at(sfLoanBrokerID) == m.brokerB.key);
    }

    void
    testTransferMissingCounterparty()
    {
        testcase("transfer without the other owner's signature");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerB_);

        env(transferTx(m), Ter(tecNO_PERMISSION));
    }

    void
    testTransferWrongCounterparty()
    {
        testcase("transfer co-signed by an account that owns neither broker");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerB_);

        auto tx = transferTx(m);
        tx[sfCounterparty] = depositor_.human();
        env(tx,
            Sig(sfCounterpartySignature, depositor_),
            Fee(env.current()->fees().base * 2),
            Ter(tecNO_PERMISSION));
    }

    void
    testTransferNotOwner()
    {
        testcase("transfer submitted by an account that owns neither broker");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);

        env(loan::transfer(depositor_, m.loan.key, m.brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferSameBroker()
    {
        testcase("transfer to the loan's own broker");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);

        env(loan::transfer(ownerA_, m.loan.key, m.brokerA.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferSameVault()
    {
        testcase("transfer to a broker of the same vault");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);
        auto const sibling = makeBroker(env, ownerA_, m.vaultA);

        env(loan::transfer(ownerA_, m.loan.key, sibling.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferAssetMismatch()
    {
        testcase("transfer to a vault of another asset");
        using namespace jtx;

        Env env(*this, all_);
        Account const issuer{"issuer"};
        fundAccounts(env);
        env.fund(XRP(1'000), issuer);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanTransfer});
        PrettyAsset const mpt = mptt.issuanceID();
        mptt.authorize({.account = depositor_});
        env(pay(issuer, depositor_, mpt(10'000)));
        env.close();

        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerA_, depositor_, mpt(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(env, ownerA_, vaultB);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecWRONG_ASSET));
    }

    void
    testTransferAccountingMismatch()
    {
        testcase("transfer to a vault with other accounting");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env.enableFeature(featureLendingProtocolV1_1);
        env.close();
        Vault const vault{env};
        auto [txB, vaultB, subB] = vault.createClosedEnded({.owner = ownerA_, .asset = xrpIssue()});
        env(txB);
        env.close();
        env(vault.deposit({.depositor = depositor_, .id = vaultB.key, .amount = XRP(10'000)}));
        env.close();
        vault.closePastSubscription(subB);
        auto const brokerB = makeBroker(env, ownerA_, vaultB);

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferFeeRateMismatch()
    {
        testcase("transfer to a broker with another management fee rate");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB =
            makeBroker(env, ownerA_, vaultB, loan_broker::kManagementFeeRate(TenthBips16(100)));
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferDefaulted()
    {
        testcase("transfer of a defaulted loan");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_, {.paymentInterval = 60});
        env.close(std::chrono::seconds{60 + 60 + 1});
        env(loan::manage(ownerA_, m.loan.key, tfLoanDefault));
        env.close();

        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferImpaired()
    {
        testcase("transfer of an impaired loan");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_, {.paymentInterval = 60});
        env.close(std::chrono::seconds{61});
        env(loan::manage(ownerA_, m.loan.key, tfLoanImpair));
        env.close();

        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferLate()
    {
        testcase("transfer of a late loan");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_, {.paymentInterval = 60});
        env.close(std::chrono::seconds{61});

        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferPaidOff()
    {
        testcase("transfer of a paid-off loan");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_, {.interestRate = 0, .paymentTotal = 1});
        env(loan::pay(borrower_, m.loan.key, XRP(500)));
        env.close();
        BEAST_EXPECT(env.le(m.loan)->at(sfPaymentRemaining) == 0);

        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferBorrowerIsBuyer()
    {
        testcase("transfer to a broker owned by the borrower");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, borrower_, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(env, borrower_, vaultB);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env(loan::transfer(ownerA_, loan.key, brokerB.key),
            Sig(sfCounterpartySignature, borrower_),
            Fee(env.current()->fees().base * 2),
            Ter(tecNO_PERMISSION));
    }

    void
    testTransferBuyerNotInvesting()
    {
        testcase("transfer to a closed-ended vault outside its Investment phase");
        using namespace jtx;

        Env env(*this, all_ | featureLendingProtocolV1_1);
        fundAccounts(env);

        Vault const vault{env};
        auto [txA, vaultA, subA] = vault.createClosedEnded({.owner = ownerA_, .asset = xrpIssue()});
        env(txA);
        env.close();
        env(vault.deposit({.depositor = depositor_, .id = vaultA.key, .amount = XRP(10'000)}));
        env.close();
        vault.closePastSubscription(subA);
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {.paymentInterval = 3'600});

        auto [txB, vaultB, subB] = vault.createClosedEnded(
            {.owner = ownerA_, .asset = xrpIssue(), .subscriptionOffset = std::chrono::hours{1}});
        env(txB);
        env.close();
        env(vault.deposit({.depositor = depositor_, .id = vaultB.key, .amount = XRP(10'000)}));
        env.close();
        auto const brokerB = makeBroker(env, ownerA_, vaultB);

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecNO_PERMISSION));
    }

    void
    testTransferFrozen()
    {
        testcase("transfer while the asset is frozen");
        using namespace jtx;

        Env env(*this, all_);
        Account const issuer{"issuer"};
        fundAccounts(env);
        env.fund(XRP(1'000), issuer);
        env.close();
        env(fset(issuer, asfDefaultRipple));
        env.close();
        auto const usd = issuer["USD"];
        for (auto const& account : {ownerA_, depositor_, borrower_})
            env(trust(account, usd(1'000'000)));
        env.close();
        env(pay(issuer, depositor_, usd(100'000)));
        env.close();

        auto const vaultA = makeVault(env, ownerA_, depositor_, usd(10'000));
        auto const vaultB = makeVault(env, ownerA_, depositor_, usd(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(env, ownerA_, vaultB);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {.principal = usd(500)});

        env(fset(issuer, asfGlobalFreeze));
        env.close();
        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecFROZEN));
    }

    void
    testTransferInsufficientFunds()
    {
        testcase("transfer to a vault without the cash");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerA_, depositor_, XRP(100));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(env, ownerA_, vaultB);
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecINSUFFICIENT_FUNDS));
    }

    void
    testTransferDebtMaximum()
    {
        testcase("transfer beyond the buying broker's debt maximum");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB =
            makeBroker(env, ownerA_, vaultB, loan_broker::kDebtMaximum(Number(XRP(100).value())));
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecLIMIT_EXCEEDED));
    }

    void
    testTransferInsufficientCover()
    {
        testcase("transfer beyond the buying broker's cover");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(
            env,
            ownerA_,
            vaultB,
            loan_broker::kCoverRateMinimum(TenthBips32(10'000)),
            loan_broker::kCoverRateLiquidation(TenthBips32(10'000)));
        auto const loan = makeLoan(env, ownerA_, borrower_, brokerA, {});

        env(loan::transfer(ownerA_, loan.key, brokerB.key), Ter(tecINSUFFICIENT_FUNDS));
    }

    void
    testTransferMalformed()
    {
        testcase("transfer malformed");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);

        env(loan::transfer(ownerA_, uint256{}, m.brokerB.key), Ter(temINVALID));
        env(loan::transfer(ownerA_, m.loan.key, uint256{}), Ter(temINVALID));
        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key, tfLoanDefault),
            Ter(temINVALID_FLAG));
    }

    void
    testTransferNoLoan()
    {
        testcase("transfer of a missing loan");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);

        env(loan::transfer(ownerA_, uint256{7}, m.brokerB.key), Ter(tecNO_ENTRY));
    }

    void
    testTransferNoBroker()
    {
        testcase("transfer to a missing broker");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);

        env(loan::transfer(ownerA_, m.loan.key, uint256{7}), Ter(tecNO_ENTRY));
    }

    void
    testBorrowerPaysAfterTransfer()
    {
        testcase("the borrower pays the buying vault after a transfer");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_, {.interestRate = 0, .paymentTotal = 1});
        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key));
        env.close();

        auto const availableA = env.le(m.vaultA)->at(sfAssetsAvailable);
        auto const availableB = env.le(m.vaultB)->at(sfAssetsAvailable);
        env(loan::pay(borrower_, m.loan.key, XRP(500)));
        env.close();
        BEAST_EXPECT(env.le(m.vaultA)->at(sfAssetsAvailable) == availableA);
        BEAST_EXPECT(env.le(m.vaultB)->at(sfAssetsAvailable) == availableB + XRP(500).value());
        BEAST_EXPECT(env.le(m.loan)->at(sfPaymentRemaining) == 0);
    }

    void
    testDefaultAfterTransfer()
    {
        testcase("a default after a transfer draws the buying broker's cover");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const vaultA = makeVault(env, ownerA_, depositor_, XRP(10'000));
        auto const vaultB = makeVault(env, ownerB_, depositor_, XRP(10'000));
        auto const brokerA = makeBroker(env, ownerA_, vaultA);
        auto const brokerB = makeBroker(
            env,
            ownerB_,
            vaultB,
            loan_broker::kCoverRateMinimum(TenthBips32(10'000)),
            loan_broker::kCoverRateLiquidation(TenthBips32(10'000)));
        env(loan_broker::coverDeposit(ownerB_, brokerB.key, XRP(1'000)));
        env.close();
        auto const loan = makeLoan(
            env,
            ownerA_,
            borrower_,
            brokerA,
            {.interestRate = 0, .paymentTotal = 1, .paymentInterval = 60});

        env(loan::transfer(ownerB_, loan.key, brokerB.key),
            Sig(sfCounterpartySignature, ownerA_),
            Fee(env.current()->fees().base * 2));
        env.close();
        auto const cover = env.le(brokerB)->at(sfCoverAvailable);
        env.close(std::chrono::seconds{60 + 60 + 1});
        env(loan::manage(ownerB_, loan.key, tfLoanDefault));
        env.close();
        BEAST_EXPECT(env.le(brokerB)->at(sfCoverAvailable) < cover);
    }

    void
    testSellerDeletesBrokerAfterTransfer()
    {
        testcase("the selling broker can be deleted after its last loan moves");
        using namespace jtx;

        Env env(*this, all_);
        fundAccounts(env);
        auto const m = makeMarket(env, ownerA_);
        env(loan::transfer(ownerA_, m.loan.key, m.brokerB.key));
        env.close();

        env(loan_broker::del(ownerA_, m.brokerA.key));
        env.close();
        BEAST_EXPECT(!env.le(m.brokerA));
    }

public:
    void
    run() override
    {
        testDisabled();
        testTransferSameOwner();
        testTransferCashBasis();
        testTransferCounterparty();
        testTransferMissingCounterparty();
        testTransferWrongCounterparty();
        testTransferNotOwner();
        testTransferSameBroker();
        testTransferSameVault();
        testTransferAssetMismatch();
        testTransferAccountingMismatch();
        testTransferFeeRateMismatch();
        testTransferDefaulted();
        testTransferImpaired();
        testTransferLate();
        testTransferPaidOff();
        testTransferBorrowerIsBuyer();
        testTransferBuyerNotInvesting();
        testTransferFrozen();
        testTransferInsufficientFunds();
        testTransferDebtMaximum();
        testTransferInsufficientCover();
        testTransferMalformed();
        testTransferNoLoan();
        testTransferNoBroker();
        testBorrowerPaysAfterTransfer();
        testDefaultAfterTransfer();
        testSellerDeletesBrokerAfterTransfer();
    }
};

BEAST_DEFINE_TESTSUITE(LoanTransfer, tx, xrpl);

}  // namespace xrpl::test
