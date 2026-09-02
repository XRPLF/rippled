#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

namespace xrpl {

// A plain VaultDeposit donation can break the loss invariant by decreasing
// assetsTotal - assetsAvailable.
//
// A donation adds the same amount to sfAssetsTotal and sfAssetsAvailable, but each is a
// separate field rounded on its own to the 16-digit IOU grid. When the two totals have
// different low digits, the exact sums round in opposite directions, so
// assetsTotal - assetsAvailable decreases by up to 1 ULP. A valid loan-written state with
// lossUnrealized exactly equal to that difference then fails "loss must not exceed
// assetsTotal - assetsAvailable" after the donation, and a legitimate donation dies with
// tecINVARIANT_FAILED. Present on develop too (the invariant and the two-field update are
// identical), so not a regression.
//
// The test expects the donation to succeed and fails on tecINVARIANT_FAILED. Manual suite.
class VaultDepositLossInvariantRounding_test : public beast::unit_test::Suite
{
    // Force a valid loan-written pre-state: loss == assetsTotal - assetsAvailable exactly.
    // Every value is 16-digit representable, so staging is exact.
    void
    stageState(
        test::jtx::Env& env,
        Keylet const& vaultKeylet,
        Number const& assetsTotal,
        Number const& assetsAvailable,
        Number const& loss)
    {
        BEAST_EXPECT(env.app().getOpenLedger().modify(  //
            [&](OpenView& view, beast::Journal) -> bool {
                Sandbox sb(&view, TapNone);
                auto v = sb.peek(vaultKeylet);
                if (!v)
                    return false;
                v->at(sfAssetsTotal) = assetsTotal;
                v->at(sfAssetsAvailable) = assetsAvailable;
                v->at(sfLossUnrealized) = loss;
                sb.update(v);
                sb.apply(view);
                return true;
            }));
    }

    //   pre-state  assetsTotal      2.000000000000001
    //              assetsAvailable  1.000000000000006
    //              loss             0.999999999999995   (== assetsTotal - assetsAvailable, valid)
    //   donate 50 (added to each field, so the difference should not change)
    //   total      2.000000000000001 + 50 = 52.000000000000001 -> stores 52          (rounds down)
    //   available  1.000000000000006 + 50 = 51.000000000000006 -> stores 51.00000000000001 (up)
    //   diff       52 - 51.00000000000001 = 0.99999999999999  <  loss 0.999999999999995
    //   result     tecINVARIANT_FAILED, should be tesSUCCESS
    void
    testDonationBreaksLossInvariant()
    {
        using namespace test::jtx;
        testcase("donation breaks the loss invariant by rounding");

        Env env(*this);
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        env.fund(XRP(1'000'000), owner, issuer);
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(1'000'000)));
        env.close();
        env(pay(issuer, owner, asset(100)));
        env.close();

        // Seed so the vault has outstanding shares (a donation needs them), then force the
        // lawful loss pre-state.
        Vault const vault{env};
        auto const [tx, keylet] = vault.create({.owner = owner, .asset = asset.raw()});
        env(tx);
        env.close();
        env(Vault::deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
        env.close();
        stageState(
            env,
            keylet,
            Number{2'000'000'000'000'001LL, -15},
            Number{1'000'000'000'000'006LL, -15},
            Number{999'999'999'999'995LL, -15});

        env(Vault::deposit(
                {.depositor = owner,
                 .id = keylet.key,
                 .amount = asset(50),
                 .flags = tfVaultDonate}),
            Ter(tesSUCCESS));
    }

    void
    run() override
    {
        testDonationBreaksLossInvariant();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(VaultDepositLossInvariantRounding, formal_verification, xrpl);

}  // namespace xrpl
