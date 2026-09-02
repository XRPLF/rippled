#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <string>

namespace xrpl {

// A vault debit too small to change the stored total dies on an invariant, not a clean error.
//
// A clawback recovery or a withdrawal payout can be smaller than half a precision step of
// sfAssetsTotal, so subtracting it rounds the balance back unchanged. No transactor check
// rejects this, the shares move, and ValidVault then fails the transaction with "must
// decrease vault balance": the user pays a fee and gets tecINVARIANT_FAILED plus a fatal
// log, where an upfront tecPRECISION_LOSS is expected (as in the zero-share case).
class VaultDustDebit_test : public beast::unit_test::Suite
{
    struct VaultData
    {
        Number assetsTotal;
        Number assetsAvailable;
        Number pseudoLine;  // vault pseudo-account trust line
        std::uint64_t sharesTotal{};
        std::uint64_t holderShares{};
    };

    static VaultData
    readVaultData(test::jtx::Env& env, Keylet const& vaultKeylet)
    {
        using namespace test::jtx;
        auto const sle = env.le(vaultKeylet);
        auto const shareMptId = sle->at(sfShareMPTID);
        PrettyAsset const asset = Account{"issuer"}["USD"];
        Account const pseudo{"pseudo", sle->at(sfAccount)};
        return {
            .assetsTotal = sle->at(sfAssetsTotal),
            .assetsAvailable = sle->at(sfAssetsAvailable),
            .pseudoLine = static_cast<Number>(env.balance(pseudo, asset.raw()).value()),
            .sharesTotal = env.le(keylet::mptokenIssuance(shareMptId))->at(sfOutstandingAmount),
            .holderShares =
                env.le(keylet::mptoken(shareMptId, Account{"holder"}))->at(sfMPTAmount)};
    }

    static void
    vaultDeposit(
        test::jtx::Env& env,
        Keylet const& vaultKeylet,
        test::jtx::Account const& depositor,
        Number const& amount,
        bool isDonation = false)
    {
        using namespace test::jtx;
        PrettyAsset const asset = Account{"issuer"}["USD"];
        Vault::DepositArgs args{
            .depositor = depositor, .id = vaultKeylet.key, .amount = asset(amount)};
        if (isDonation)
            args.flags = tfVaultDonate;
        env(Vault::deposit(args));
        env.close();
    }

    // Fund everyone, the holder seeds (scale 6, shares mint exactly), the owner donates to
    // set the share price without minting shares.
    Keylet
    buildSeededVault(test::jtx::Env& env, Number const& seed, Number const& donation)
    {
        using namespace test::jtx;
        Account const owner{"owner"};
        Account const issuer{"issuer"};
        Account const holder{"holder"};
        env.fund(XRP(1'000'000), owner, issuer, holder);
        env.close();
        // IOU clawback must be enabled while the issuer has no trust line yet.
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const asset = issuer["USD"];
        env(trust(owner, asset(100'000'000'000'000LL)));
        env(trust(holder, asset(100'000'000'000'000LL)));
        env.close();
        env(pay(issuer, holder, asset(seed)));
        env(pay(issuer, owner, asset(donation)));
        env.close();

        Vault const vault{env};
        auto const [tx, keylet] = vault.create({.owner = owner, .asset = asset.raw()});
        env(tx);
        env.close();
        vaultDeposit(env, keylet, holder, seed);
        vaultDeposit(env, keylet, owner, donation, true);

        VaultData const initial = readVaultData(env, keylet);
        BEAST_EXPECTS(initial.assetsTotal == seed + donation, to_string(initial.assetsTotal));
        BEAST_EXPECTS(initial.pseudoLine == seed + donation, to_string(initial.pseudoLine));
        return keylet;
    }

    // The rejection must leave every stored balance and share count untouched.
    void
    expectUnchanged(VaultData const& before, VaultData const& after)
    {
        BEAST_EXPECTS(after.assetsTotal == before.assetsTotal, to_string(after.assetsTotal));
        BEAST_EXPECTS(
            after.assetsAvailable == before.assetsAvailable, to_string(after.assetsAvailable));
        BEAST_EXPECTS(after.pseudoLine == before.pseudoLine, to_string(after.pseudoLine));
        BEAST_EXPECTS(after.sharesTotal == before.sharesTotal, std::to_string(after.sharesTotal));
        BEAST_EXPECTS(
            after.holderShares == before.holderShares, std::to_string(after.holderShares));
    }

    // Claw back an amount whose recovery cannot change the stored total. The clean outcome
    // is tecPRECISION_LOSS, but C++ runs into the invariant.
    void
    runDustClawback(Number const& seed, Number const& donation, Number const& clawAmount)
    {
        using namespace test::jtx;
        testcase("clawback " + to_string(clawAmount) + " from " + to_string(seed + donation));

        Env env(*this);
        Keylet const vaultKeylet = buildSeededVault(env, seed, donation);
        Account const issuer{"issuer"};
        PrettyAsset const asset = issuer["USD"];

        VaultData const before = readVaultData(env, vaultKeylet);
        env(Vault::clawback(
                {.issuer = issuer,
                 .id = vaultKeylet.key,
                 .holder = Account{"holder"},
                 .amount = asset(clawAmount)}),
            Ter(tecPRECISION_LOSS));
        env.close();
        expectUnchanged(before, readVaultData(env, vaultKeylet));
    }

    // Redeem a share count whose payout cannot change the stored total. The clean outcome
    // is tecPRECISION_LOSS, but C++ runs into the invariant.
    void
    runDustWithdraw(Number const& seed, Number const& donation, std::int64_t redeemShares)
    {
        using namespace test::jtx;
        testcase(
            "withdraw " + std::to_string(redeemShares) + " shares from " +
            to_string(seed + donation));

        Env env(*this);
        Keylet const vaultKeylet = buildSeededVault(env, seed, donation);
        MPTIssue const share{env.le(vaultKeylet)->at(sfShareMPTID)};

        VaultData const before = readVaultData(env, vaultKeylet);
        env(Vault::withdraw(
                {.depositor = Account{"holder"},
                 .id = vaultKeylet.key,
                 .amount = STAmount{share, redeemShares}}),
            Ter(tecPRECISION_LOSS));
        env.close();
        expectUnchanged(before, readVaultData(env, vaultKeylet));
    }

    // Caught by the 16-digit STAmount round: the exact new total fits Number, then rounds
    // back when the field is stored.
    //   vault    AssetsTotal 2e12 (seed 1e12 + donate 1e12), 1e18 shares -> 1 share = 2e-6
    //   debit    clawback 2e-6, or withdraw 1 share, both take 2e-6
    //   exact    2e12 - 2e-6 = 1999999999999.999998    (19 digits, fits Number)
    //   stored   2e12                                  (16-digit round brings it back)
    //   result   tecINVARIANT_FAILED, should be tecPRECISION_LOSS
    void
    testBelowStoredPrecision()
    {
        runDustClawback(Number{1, 12}, Number{1, 12}, Number{2, -6});
        runDustWithdraw(Number{1, 12}, Number{1, 12}, 1);
    }

    // Caught one layer down, by the 19-digit Number subtraction itself.
    //   vault    AssetsTotal 1.5e13 (seed 9.2e12 + donate 5.8e12), 9.2e18 shares
    //   share    1 share = 1.63e-6
    //   debit    clawback 2e-6, or withdraw 1 share, both take ~1.63e-6
    //   exact    1.5e13 - 1.63e-6 needs 20 digits -> rounds back to 1.5e13 in Number
    //   result   tecINVARIANT_FAILED, should be tecPRECISION_LOSS
    void
    testBelowNumberPrecision()
    {
        runDustClawback(Number{92, 11}, Number{58, 11}, Number{2, -6});
        runDustWithdraw(Number{92, 11}, Number{58, 11}, 1);
    }

    void
    run() override
    {
        testBelowStoredPrecision();
        testBelowNumberPrecision();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(VaultDustDebit, formal_verification, xrpl);

}  // namespace xrpl
