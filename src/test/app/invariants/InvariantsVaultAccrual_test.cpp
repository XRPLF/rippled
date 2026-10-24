#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/sig.h>
#include <test/jtx/tags.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * ValidVaultAccrual, XLS Vault Continuous Accrual section 7: each check fires
 * on a vault whose accrual or struck-price state moved where it may not.
 */
class InvariantsVaultAccrual_test : public InvariantsBase
{
    // Accrual is available on a CashBasis Vault, so FixedPrecision stays off.
    FeatureBitset const features_{test::jtx::testableAmendments() - featureLendingProtocolV1_2};

    static constexpr std::uint32_t kInterval = 86'400;
    static constexpr std::uint32_t kWindow = 3'600;

    // Runs one check against the vault preclose leaves behind, with precheck
    // editing that vault as a transaction of txType would.
    void
    check(
        std::string const& expectedLog,
        TxType txType,
        std::function<bool(test::jtx::Account const&, test::jtx::Env&, Keylet&)> const& setup,
        std::function<void(SLE::Ref)> const& edit)
    {
        Keylet vaultKeylet = keylet::vault(UInt256{});
        doInvariantCheck(
            makeEnv(features_),
            {expectedLog},
            [&](test::jtx::Account const&, test::jtx::Account const&, ApplyContext& ac) {
                auto const sle = ac.view().peek(vaultKeylet);
                if (!sle)
                    return false;
                edit(sle);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            STTx{txType, [&](STObject& tx) { tx.setFieldH256(sfVaultID, vaultKeylet.key); }},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](test::jtx::Account const& a1, test::jtx::Account const&, test::jtx::Env& env) {
                return setup(a1, env, vaultKeylet);
            },
            TxAccount::A1);
    }

    // An empty XRP vault with no accrual state.
    static bool
    freshVault(test::jtx::Account const& owner, test::jtx::Env& env, Keylet& vaultKeylet)
    {
        using namespace test::jtx;
        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(tx);
        vaultKeylet = keylet;
        return true;
    }

    // A rolling XRP vault whose second window's price has been struck.
    static bool
    struckVault(test::jtx::Account const& owner, test::jtx::Env& env, Keylet& vaultKeylet)
    {
        using namespace test::jtx;
        Vault const vault{env};
        auto const start = static_cast<std::uint32_t>(env.now().time_since_epoch().count()) + 60;
        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = xrpIssue(),
             .vaultKind = std::to_underlying(VaultKind::Rolling),
             .subscriptionDate = start,
             .dealingInterval = kInterval,
             .dealingWindow = kWindow});
        env(tx);
        env.close();
        vaultKeylet = keylet;

        env.close(NetClock::time_point{NetClock::duration{start + 10}});
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = XRP(100)}));
        env.close(NetClock::time_point{NetClock::duration{start + kInterval + 10}});
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = XRP(100)}));
        env.close();

        auto const sle = env.le(keylet);
        return sle && sle->at(sfStruckUntil) != 0;
    }

    // An accrual XRP vault lending to its owner's counterparty, so it carries
    // a stamped sfLastAccrualTime.
    static bool
    lendingVault(test::jtx::Account const& owner, test::jtx::Env& env, Keylet& vaultKeylet)
    {
        using namespace test::jtx;
        Account const borrower{"borrower"};
        env.fund(XRP(1'000), borrower);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(tx);
        env.close();
        vaultKeylet = keylet;
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = XRP(500)}));
        env.close();

        auto const brokerKeylet =
            keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
        env(loan_broker::set(owner, keylet.key), Fee(kIncrement));
        env.close();

        env(loan::set(borrower, brokerKeylet.key, XRP(100).value()),
            loan::kCounterparty(owner),
            loan::kInterestRate(TenthBips32{10'000}),
            loan::kPaymentTotal(4),
            loan::kPaymentInterval(600),
            Sig(sfCounterpartySignature, owner),
            Fee(env.current()->fees().base * 2));
        env.close();

        auto const sle = env.le(keylet);
        return sle && sle->at(sfLastAccrualTime) != 0;
    }

    void
    testNegativeState()
    {
        testcase("accrual rate and unearned interest are never negative");
        check("vault accrual rate is negative", ttVAULT_SET, freshVault, [](SLE::Ref sle) {
            sle->at(sfAccrualRate) = Number{-1};
        });
        check("vault unearned interest is negative", ttVAULT_SET, freshVault, [](SLE::Ref sle) {
            sle->at(sfUnearnedInterest) = Number{-1};
        });
    }

    void
    testSettlement()
    {
        testcase("settlement only moves forward and only in a loan transaction");
        check(
            "vault last accrual time moved backwards", ttLOAN_PAY, lendingVault, [](SLE::Ref sle) {
                std::uint32_t const stamped = sle->at(sfLastAccrualTime);
                sle->at(sfLastAccrualTime) = stamped - 1;
            });
        check(
            "vault settled outside a loan transaction", ttVAULT_SET, freshVault, [](SLE::Ref sle) {
                sle->at(sfLastAccrualTime) = 100;
            });
        check(
            "dealing transaction changed vault accrual state",
            ttVAULT_DEPOSIT,
            freshVault,
            [](SLE::Ref sle) { sle->at(sfAccrualRate) = Number{1}; });
    }

    void
    testStruckPrice()
    {
        testcase("struck price moves only forward, once per window, in a deal");
        check(
            "vault price struck outside a dealing transaction",
            ttVAULT_SET,
            freshVault,
            [](SLE::Ref sle) {
                sle->at(sfStruckPrice) = Number{1};
                sle->at(sfStruckUntil) = 100;
            });
        check(
            "vault struck window moved backwards", ttVAULT_DEPOSIT, struckVault, [](SLE::Ref sle) {
                std::uint32_t const until = sle->at(sfStruckUntil);
                sle->at(sfStruckUntil) = until - 1;
            });
        check(
            "vault price struck twice in one window",
            ttVAULT_DEPOSIT,
            struckVault,
            [](SLE::Ref sle) {
                sle->at(sfStruckPrice) = Number{sle->at(sfStruckPrice)} * Number{2};
            });
    }

public:
    void
    run() override
    {
        testNegativeState();
        testSettlement();
        testStruckPrice();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsVaultAccrual, app, xrpl);

}  // namespace xrpl::test
