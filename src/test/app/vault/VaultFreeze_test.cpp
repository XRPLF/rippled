#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultFreeze_test : public VaultTestBase
{
private:
    void
    testVaultDepositFreezeIOU()
    {
        using namespace test::jtx;
        testcase("VaultDeposit IOU freeze checks");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Env env{*this};
        Vault vault{env};

        env.fund(XRP(100'000), issuer, owner);
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1'000'000), owner);
        env(pay(issuer, owner, asset(100'000)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();
        auto const vaultAcct = Account("vault", env.le(keylet)->at(sfAccount));

        // Initial deposit so the vault pseudo-account has a trustline
        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
        env.close();

        auto runTests = [&]() {
            auto const fix330Enabled = env.current()->rules().enabled(fixCleanup3_3_0);

            // Global freeze
            {
                testcase("VaultDeposit IOU global freeze");
                env(fset(issuer, asfGlobalFreeze));
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(tecFROZEN));
                env(fclear(issuer, asfGlobalFreeze));
            }

            // Depositor freeze
            {
                testcase("VaultDeposit IOU depositor freeze");
                env(trust(issuer, asset(0), owner, tfSetFreeze));
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(tecFROZEN));
                env(trust(issuer, asset(0), owner, tfClearFreeze));
            }

            // Depositor deep freeze
            {
                testcase("VaultDeposit IOU depositor deep freeze");
                env(trust(issuer, asset(0), owner, tfSetFreeze | tfSetDeepFreeze));
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(tecFROZEN));
                env(trust(issuer, asset(0), owner, tfClearFreeze | tfClearDeepFreeze));
            }

            // Depositor holder-side deep freeze
            {
                testcase("VaultDeposit IOU depositor holder-side deep freeze");
                env(trust(owner, asset(1'000'000), tfSetFreeze | tfSetDeepFreeze));
                auto const fix340Enabled = env.current()->rules().enabled(fixCleanup3_4_0);
                // Post-fixCleanup3_4_0: checkDeepFrozen catches any-side deep freeze.
                // Pre-fix: ZeroIfFrozen still reports the holder as unfunded.
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(fix340Enabled ? TER(tecFROZEN) : TER(tecINSUFFICIENT_FUNDS)));
                env(trust(owner, asset(1'000'000), tfClearFreeze | tfClearDeepFreeze));
            }

            // Vault-account freeze
            // Post-fix: checkDepositFreeze catches it → tecFROZEN
            // Pre-fix: not checked directly, but the transitive share
            //          check triggers → tecLOCKED
            {
                testcase("VaultDeposit IOU pseudo-account freeze");
                auto trustSet = [&]() {
                    json::Value jv;
                    jv[jss::Account] = issuer.human();
                    {
                        auto& ja = jv[jss::LimitAmount] =
                            asset(0).value().getJson(JsonOptions::Values::None);
                        ja[jss::issuer] = toBase58(vaultAcct.id());
                    }
                    jv[jss::TransactionType] = jss::TrustSet;
                    return jv;
                }();

                trustSet[jss::Flags] = tfSetFreeze;
                env(trustSet);
                env.close();

                TER const expected = fix330Enabled ? TER(tecFROZEN) : TER(tecLOCKED);
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(expected));

                trustSet[jss::Flags] = tfClearFreeze;
                env(trustSet);
                env.close();
            }

            // Vault-account deep freeze
            {
                testcase("VaultDeposit IOU pseudo-account deep freeze");
                auto trustSet = [&]() {
                    json::Value jv;
                    jv[jss::Account] = issuer.human();
                    {
                        auto& ja = jv[jss::LimitAmount] =
                            asset(0).value().getJson(JsonOptions::Values::None);
                        ja[jss::issuer] = toBase58(vaultAcct.id());
                    }
                    jv[jss::TransactionType] = jss::TrustSet;
                    return jv;
                }();

                trustSet[jss::Flags] = tfSetFreeze | tfSetDeepFreeze;
                env(trustSet);
                env.close();

                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(fix330Enabled ? TER(tecFROZEN) : TER(tecLOCKED)));

                trustSet[jss::Flags] = tfClearFreeze | tfClearDeepFreeze;
                env(trustSet);
                env.close();
            }

            // Clawback works while frozen
            {
                testcase("VaultDeposit IOU freeze clawback unaffected");
                env(fset(issuer, asfGlobalFreeze));
                env(vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(1)}));
                env(fclear(issuer, asfGlobalFreeze));
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
                env.close();
            }
        };

        runTests();
        env.disableFeature(fixCleanup3_3_0);
        runTests();
        env.enableFeature(fixCleanup3_3_0);
        env.disableFeature(fixCleanup3_4_0);
        runTests();
        env.enableFeature(fixCleanup3_4_0);
    }

    void
    testVaultDepositFreezeMPT()
    {
        using namespace test::jtx;
        testcase("VaultDeposit MPT lock checks");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Env env{*this};
        Vault vault{env};

        env.fund(XRP(100'000), issuer, owner);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});
        PrettyAsset const mpt{mptt.issuanceID()};

        mptt.authorize({.account = owner});
        mptt.authorize({.account = issuer, .holder = owner});
        env.close();
        env(pay(issuer, owner, mpt(100'000)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = mpt});
        env(tx);
        env.close();
        auto const vaultAcctID = env.le(keylet)->at(sfAccount);
        Account const vaultAcct("vault", vaultAcctID);

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(100)}));
        env.close();

        // For MPT isDeepFrozen == isFrozen, so all locks block in
        // both pre- and post-fix.
        auto runTests = [&]() {
            // Global lock
            {
                testcase("VaultDeposit MPT global lock");
                mptt.set({.flags = tfMPTLock});
                env.close();
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}),
                    Ter(tecLOCKED));
                mptt.set({.flags = tfMPTUnlock});
                env.close();
            }

            // Depositor individual lock
            {
                testcase("VaultDeposit MPT depositor lock");
                mptt.set({.holder = owner, .flags = tfMPTLock});
                env.close();
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}),
                    Ter(tecLOCKED));
                mptt.set({.holder = owner, .flags = tfMPTUnlock});
                env.close();
            }

            // Vault pseudo-account individual lock
            {
                testcase("VaultDeposit MPT pseudo-account lock");
                mptt.set({.holder = vaultAcct, .flags = tfMPTLock});
                env.close();
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}),
                    Ter(tecLOCKED));
                mptt.set({.holder = vaultAcct, .flags = tfMPTUnlock});
                env.close();
            }

            // Clawback works while locked
            {
                testcase("VaultDeposit MPT lock clawback unaffected");
                mptt.set({.flags = tfMPTLock});
                env.close();
                env(vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = mpt(1)}));
                mptt.set({.flags = tfMPTUnlock});
                env.close();
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}));
                env.close();
            }
        };

        runTests();
        env.disableFeature(fixCleanup3_3_0);
        runTests();
        env.enableFeature(fixCleanup3_3_0);
    }

    void
    testVaultWithdrawFreezeIOU()
    {
        using namespace test::jtx;
        testcase("VaultWithdraw IOU freeze checks");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Env env{*this};
        Vault const vault{env};

        env.fund(XRP(100'000), issuer, owner);
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1'000'000), owner);
        env(pay(issuer, owner, asset(100'000)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();
        auto const vaultAcct = Account("vault", env.le(keylet)->at(sfAccount));

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(100)}));
        env.close();

        Account const charlie{"charlie"};
        env.fund(XRP(10'000), charlie);
        env.trust(asset(1'000'000), charlie);
        env.close();

        auto runTests = [&]() {
            auto const fix330Enabled = env.current()->rules().enabled(fixCleanup3_3_0);
            // Global freeze → self-withdraw
            {
                testcase("VaultWithdraw IOU global freeze");
                env(fset(issuer, asfGlobalFreeze));
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(tecFROZEN));
                // Global freeze → withdraw to 3rd party

                auto withdrawToCharlie =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)});
                withdrawToCharlie[sfDestination] = charlie.human();
                env(withdrawToCharlie, Ter(tecFROZEN));

                env(fclear(issuer, asfGlobalFreeze));
            }

            // Vault-account freeze
            {
                testcase("VaultWithdraw IOU pseudo-account freeze");
                auto trustSet = [&]() {
                    json::Value jv;
                    jv[jss::Account] = issuer.human();
                    {
                        auto& ja = jv[jss::LimitAmount] =
                            asset(0).value().getJson(JsonOptions::Values::None);
                        ja[jss::issuer] = toBase58(vaultAcct.id());
                    }
                    jv[jss::TransactionType] = jss::TrustSet;
                    return jv;
                }();

                trustSet[jss::Flags] = tfSetFreeze;
                env(trustSet);
                env.close();

                TER const terExpected = fix330Enabled ? TER(tecFROZEN) : TER(tecLOCKED);

                // Self-withdraw
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(terExpected));
                // Withdraw to 3rd party

                auto withdrawToCharlie =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)});
                withdrawToCharlie[sfDestination] = charlie.human();
                env(withdrawToCharlie, Ter(terExpected));

                trustSet[jss::Flags] = tfClearFreeze;
                env(trustSet);
                env.close();
            }

            // Depositor freeze, self-withdraw
            {
                testcase("VaultWithdraw IOU self-withdraw freeze check");
                env(trust(issuer, asset(0), owner, tfSetFreeze));

                // Post-fix: self-withdraw allowed (submitter==dst skip)
                // Pre-fix: isFrozen(depositor, iou) catches it
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(fix330Enabled ? TER(tesSUCCESS) : TER(tecFROZEN)));

                // Depositor freeze withdraw to 3rd party
                auto withdrawTo3rd =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)});
                withdrawTo3rd[sfDestination] = charlie.human();

                // Post-fix: submitter freeze blocks withdraw to 3rd party
                // Pre-fix: submitter's IOU freeze not checked, but checkFrozen(depositor,
                // share) triggers tecLOCKED
                env(withdrawTo3rd, Ter(fix330Enabled ? TER(tecFROZEN) : TER(tecLOCKED)));

                env(trust(issuer, asset(0), owner, tfClearFreeze));
                // Replenish what was withdrawn
                if (fix330Enabled)
                {
                    env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
                }
                env.close();
            }

            // Depositor deep freeze → self-withdraw blocked
            {
                testcase("VaultWithdraw IOU depositor deep freeze");
                env(trust(issuer, asset(0), owner, tfSetFreeze | tfSetDeepFreeze));

                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(tecFROZEN));

                env(trust(issuer, asset(0), owner, tfClearFreeze | tfClearDeepFreeze));
            }

            // Holder-side deep freeze on dest → self-withdraw blocked
            {
                testcase("VaultWithdraw IOU holder-side deep freeze");
                env(trust(owner, asset(1'000'000), tfSetFreeze | tfSetDeepFreeze));
                // Post-fixCleanup3_3_0: checkWithdrawFreeze uses isDeepFrozen on dest.
                // Pre-fix: checkFrozen misses holder-side deep freeze.
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                    Ter(fix330Enabled ? TER(tecFROZEN) : TER(tesSUCCESS)));
                env(trust(owner, asset(1'000'000), tfClearFreeze | tfClearDeepFreeze));
                if (!fix330Enabled)
                {
                    env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
                    env.close();
                }
            }

            // Destination freeze → withdraw to 3rd party
            {
                testcase("VaultWithdraw IOU freeze withdraw to 3rd party");

                env(trust(issuer, asset(0), charlie, tfSetFreeze));

                // Self-withdraw unaffected by charlie's freeze
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}));

                auto withdrawToCharlie =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)});
                withdrawToCharlie[sfDestination] = charlie.human();

                // Post-fix: freeze on dst allowed
                // Pre-fix: checkFrozen(dst, iou) catches it
                env(withdrawToCharlie, Ter(fix330Enabled ? TER(tesSUCCESS) : TER(tecFROZEN)));

                env(trust(issuer, asset(0), charlie, tfClearFreeze));

                // Replenish: 1 for self-withdraw + 1 if charlie withdraw succeeded
                env(vault.deposit(
                    {.depositor = owner,
                     .id = keylet.key,
                     .amount = asset(fix330Enabled ? 2 : 1)}));
                env.close();
            }

            // Destination deep freeze → withdraw to 3rd party blocked
            {
                testcase("VaultWithdraw IOU deep freeze withdraw to 3rd party");

                env(trust(issuer, asset(0), charlie, tfSetFreeze | tfSetDeepFreeze));

                auto withdrawToCharlie =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)});
                withdrawToCharlie[sfDestination] = charlie.human();
                env(withdrawToCharlie, Ter(tecFROZEN));

                // Destination deep freeze → self-withdraw unaffected
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}));

                env(trust(issuer, asset(0), charlie, tfClearFreeze | tfClearDeepFreeze));
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
                env.close();
            }

            // Clawback works while frozen
            {
                testcase("VaultWithdraw IOU freeze clawback unaffected");
                env(fset(issuer, asfGlobalFreeze));

                env(vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = asset(1)}));

                env(fclear(issuer, asfGlobalFreeze));
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(1)}));
                env.close();
            }
        };

        runTests();
        env.disableFeature(fixCleanup3_3_0);
        runTests();
        env.enableFeature(fixCleanup3_3_0);
    }

    void
    testVaultWithdrawFreezeMPT()
    {
        using namespace test::jtx;
        testcase("VaultWithdraw MPT lock checks");

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Env env{*this};
        Vault vault{env};

        env.fund(XRP(100'000), issuer, owner);
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTRequireAuth});
        PrettyAsset const mpt{mptt.issuanceID()};

        mptt.authorize({.account = owner});
        mptt.authorize({.account = issuer, .holder = owner});
        env.close();
        env(pay(issuer, owner, mpt(100'000)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = mpt});
        env(tx);
        env.close();
        Account const vaultAcct("vault", env.le(keylet)->at(sfAccount));

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(100)}));
        env.close();

        Account const charlie{"charlie"};
        env.fund(XRP(10'000), charlie);
        env.close();
        mptt.authorize({.account = charlie});
        mptt.authorize({.account = issuer, .holder = charlie});
        env.close();

        auto runTests = [&]() {
            auto const fix330Enabled = env.current()->rules().enabled(fixCleanup3_3_0);

            // Global lock
            {
                testcase("VaultWithdraw MPT global lock");
                mptt.set({.flags = tfMPTLock});
                env.close();
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)}),
                    Ter(tecLOCKED));

                // Global lock → withdraw to issuer
                // Post-fix: bypasses freeze checks, but accountHolds
                //           on the pseudo returns 0 under global lock
                // Pre-fix: checkFrozen(dst=issuer) catches global lock
                {
                    auto withdrawToIssuer =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)});
                    withdrawToIssuer[sfDestination] = issuer.human();
                    env(withdrawToIssuer, Ter(fix330Enabled ? TER(tesSUCCESS) : TER(tecLOCKED)));
                }
                mptt.set({.flags = tfMPTUnlock});
                env.close();
                if (fix330Enabled)
                {
                    env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}));
                }
                env.close();
            }

            // Vault pseudo-account individual lock
            {
                testcase("VaultWithdraw MPT pseudo-account lock");
                mptt.set({.holder = vaultAcct, .flags = tfMPTLock});
                env.close();
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)}),
                    Ter(tecLOCKED));
                mptt.set({.holder = vaultAcct, .flags = tfMPTUnlock});
                env.close();
            }

            // Depositor individual lock → self-withdraw blocked
            // (isDeepFrozen == isFrozen for MPT)
            {
                testcase("VaultWithdraw MPT depositor lock");
                mptt.set({.holder = owner, .flags = tfMPTLock});
                env.close();
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)}),
                    Ter(tecLOCKED));
                // Depositor lock → withdraw to 3rd party also blocked
                {
                    auto withdrawToCharlie =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)});
                    withdrawToCharlie[sfDestination] = charlie.human();
                    env(withdrawToCharlie, Ter(tecLOCKED));
                }

                // Depositor lock → withdraw to issuer
                // Post-fix: issuer bypass in checkWithdrawFreezes
                // Pre-fix: checkFrozen(depositor, share) blocks transitively
                {
                    auto withdrawToIssuer =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)});
                    withdrawToIssuer[sfDestination] = issuer.human();
                    env(withdrawToIssuer, Ter(fix330Enabled ? TER(tesSUCCESS) : TER(tecLOCKED)));
                }
                mptt.set({.holder = owner, .flags = tfMPTUnlock});
                env.close();
                if (fix330Enabled)
                {
                    env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}));
                }
                env.close();
            }

            // 3rd party destination lock → withdraw to 3rd party blocked
            {
                testcase("VaultWithdraw MPT 3rd party destination lock");
                mptt.set({.holder = charlie, .flags = tfMPTLock});
                env.close();
                {
                    auto withdrawToCharlie =
                        vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)});
                    withdrawToCharlie[sfDestination] = charlie.human();
                    env(withdrawToCharlie, Ter{tecLOCKED});
                }
                // 3rd party lock → self-withdraw unaffected
                env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = mpt(1)}));
                mptt.set({.holder = charlie, .flags = tfMPTUnlock});
                env.close();
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}));
                env.close();
            }

            // Clawback works while locked
            {
                testcase("VaultWithdraw MPT lock clawback unaffected");
                mptt.set({.flags = tfMPTLock});
                env.close();
                env(vault.clawback(
                    {.issuer = issuer, .id = keylet.key, .holder = owner, .amount = mpt(1)}));
                mptt.set({.flags = tfMPTUnlock});
                env.close();
                env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = mpt(1)}));
                env.close();
            }
        };

        runTests();
        env.disableFeature(fixCleanup3_3_0);
        runTests();
        env.enableFeature(fixCleanup3_3_0);
    }

    // Focused demonstration: a depositor under an individual IOU freeze
    // can still withdraw to themselves (self-withdrawal), but is blocked from
    // withdrawing to a third party.
    //
    // Pre-fixCleanup3_3_0: both the self-withdrawal AND the third-party
    // withdrawal were blocked because the old code checked checkFrozen on the
    // destination regardless of whether it was the submitter.
    // Post-fixCleanup3_3_0: checkWithdrawFreeze skips the submitter freeze
    // check when submitter == destination, so self-withdrawal succeeds.
    void
    testVaultSelfWithdrawWhileFrozen()
    {
        testcase("VaultWithdraw IOU self-withdrawal while individually frozen");

        using namespace test::jtx;

        Account const issuer{"issuer"};
        Account const owner{"owner"};
        Account const charlie{"charlie"};
        Env env{*this};
        Vault vault{env};

        env.fund(XRP(100'000), issuer, owner, charlie);
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1'000'000), owner);
        env.trust(asset(1'000'000), charlie);
        env(pay(issuer, owner, asset(100'000)));
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = asset});
        env(tx);
        env.close();

        env(vault.deposit({.depositor = owner, .id = keylet.key, .amount = asset(10)}));
        env.close();

        auto runTests = [&]() {
            auto const fix330Enabled = env.current()->rules().enabled(fixCleanup3_3_0);

            // Set an individual freeze on the owner's IOU trustline.
            env(trust(issuer, asset(0), owner, tfSetFreeze));
            env.close();

            // Self-withdrawal: submitter == destination, so the submitter
            // freeze check is skipped.
            // Post-fix: tesSUCCESS.  Pre-fix: tecFROZEN.
            env(vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)}),
                Ter(fix330Enabled ? TER(tesSUCCESS) : TER(tecFROZEN)));

            // Withdrawal to a third party is blocked: submitter != destination
            // so the submitter freeze check applies.
            {
                auto withdrawToCharlie =
                    vault.withdraw({.depositor = owner, .id = keylet.key, .amount = asset(1)});
                withdrawToCharlie[sfDestination] = charlie.human();
                // Post-fix: tecFROZEN (checkIndividualFrozen on submitter).
                // Pre-fix: tecLOCKED (isFrozen on the vault share).
                env(withdrawToCharlie, Ter(fix330Enabled ? TER(tecFROZEN) : TER(tecLOCKED)));
            }

            env(trust(issuer, asset(0), owner, tfClearFreeze));
            env.close();
        };

        runTests();
        env.disableFeature(fixCleanup3_3_0);
        runTests();
        env.enableFeature(fixCleanup3_3_0);
    }

public:
    void
    run() override
    {
        testVaultDepositFreezeIOU();
        testVaultDepositFreezeMPT();
        testVaultWithdrawFreezeIOU();
        testVaultWithdrawFreezeMPT();
        testVaultSelfWithdrawWhileFrozen();
    }
};

BEAST_DEFINE_TESTSUITE(VaultFreeze, app, xrpl);

}  // namespace xrpl
