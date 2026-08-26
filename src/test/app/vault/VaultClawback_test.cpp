#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/escrow.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/sig.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_forwards.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/protocol/jss.h>

#include <chrono>
#include <string>
#include <tuple>
#include <utility>

namespace xrpl {

class VaultClawback_test : public VaultTestBase
{
private:
    void
    testVaultClawbackBurnShares()
    {
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;
        Env env(*this, beast::Severity::Warning);

        auto const vaultAssetBalance = [&](Keylet const& vaultKeylet) {
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault != nullptr);

            return std::make_pair(sleVault->at(sfAssetsAvailable), sleVault->at(sfAssetsTotal));
        };

        auto const vaultShareBalance = [&](Keylet const& vaultKeylet) {
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault != nullptr);

            auto const sleIssuance = env.le(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
            BEAST_EXPECT(sleIssuance != nullptr);

            return sleIssuance->at(sfOutstandingAmount);
        };

        // Under featureLendingProtocolV1_1 LoanBrokerSet::preclaim only
        // accepts closed-ended vaults, so build vaults in this suite as
        // closed-ended and advance past SubscriptionDate before creating
        // brokers/loans. VaultClawback itself is not phase-gated. The
        // subscription offset must be large enough that the deposit
        // ledger close does not accidentally push us past SubscriptionDate
        // (which would land the deposit in Investment phase and fail).
        auto const setupVault = [&](PrettyAsset const& asset,
                                    Account const& owner,
                                    Account const& depositor) -> std::pair<Vault, Keylet> {
            Vault const vault{env};

            auto const& [tx, vaultKeylet, subscriptionDate] = vault.createClosedEnded(
                {.owner = owner, .asset = asset, .subscriptionOffset = std::chrono::seconds{60}});
            env(tx, Ter(tesSUCCESS));
            env.close();

            auto const& vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);

            Asset const share = vaultSle->at(sfShareMPTID);

            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            // Move past SubscriptionDate so LoanBrokerSet/LoanSet run in
            // the Investment phase.
            vault.closePastSubscription(subscriptionDate);

            auto const& [availablePreDefault, totalPreDefault] = vaultAssetBalance(vaultKeylet);
            BEAST_EXPECT(availablePreDefault == totalPreDefault);
            BEAST_EXPECT(availablePreDefault == asset(100).value());

            // attempt to clawback shares while there are assets fails
            env(vault.clawback(
                    {.issuer = owner,
                     .id = vaultKeylet.key,
                     .holder = depositor,
                     .amount = share(0).value()}),
                Ter(tecNO_PERMISSION));
            env.close();

            auto const& sharesAvailable = vaultShareBalance(vaultKeylet);
            auto const& brokerKeylet =
                keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));

            env(set(owner, vaultKeylet.key));
            env.close();

            auto const& loanKeylet = keylet::loan(brokerKeylet.key, SeqProxy::rawSequence(1));

            // Create a simple Loan for the full amount of Vault assets
            env(set(depositor, brokerKeylet.key, asset(100).value()),
                loan::kInterestRate(TenthBips32(0)),
                kGracePeriod(60),
                kPaymentInterval(120),
                kPaymentTotal(10),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            // attempt to clawback shares while there assetsAvailable == 0 and
            // assetsTotal > 0 fails
            env(vault.clawback(
                    {.issuer = owner,
                     .id = vaultKeylet.key,
                     .holder = depositor,
                     .amount = share(0).value()}),
                Ter(tecNO_PERMISSION));
            env.close();

            env.close(std::chrono::seconds{120 + 60});

            env(manage(owner, loanKeylet.key, tfLoanDefault), Ter(tesSUCCESS));

            auto const& [availablePostDefault, totalPostDefault] = vaultAssetBalance(vaultKeylet);

            BEAST_EXPECT(availablePostDefault == totalPostDefault);
            BEAST_EXPECT(availablePostDefault == asset(0).value());
            BEAST_EXPECT(vaultShareBalance(vaultKeylet) == sharesAvailable);

            return std::make_pair(vault, vaultKeylet);
        };

        auto const testCase = [&](PrettyAsset const& asset,
                                  std::string const& prefix,
                                  Account const& owner,
                                  Account const& depositor) {
            {
                testcase("VaultClawback (share) - " + prefix + " owner asset clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                // when asset is XRP or owner is not issuer clawback fail
                // when owner is issuer precision loss occurs as vault is
                // empty
                auto const expectedTer = [&]() {
                    if (asset.native())
                        return Ter(temMALFORMED);
                    if (asset.raw().getIssuer() != owner.id())
                        return Ter(tecNO_PERMISSION);
                    return Ter(tecPRECISION_LOSS);
                }();
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(100).value(),
                    }),
                    expectedTer);
                env.close();
            }

            {
                testcase(
                    "VaultClawback (share) - " + prefix + " owner incomplete share clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = share(1).value(),
                    }),
                    Ter(tecLIMIT_EXCEEDED));
                env.close();
            }

            {
                testcase(
                    "VaultClawback (share) - " + prefix +
                    " owner implicit complete share clawback");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    // when owner is issuer implicit clawback fails
                    asset.native() || asset.raw().getIssuer() != owner.id() ? Ter(tesSUCCESS)
                                                                            : Ter(tecWRONG_ASSET));
                env.close();
            }

            {
                testcase(
                    "VaultClawback (share) - " + prefix +
                    " owner explicit complete share clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();
            }
            {
                testcase("VaultClawback (share) - " + prefix + " owner can clawback own shares");
                auto [vault, vaultKeylet] = setupVault(asset, owner, owner);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = owner,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();
            }

            {
                testcase("VaultClawback (share) - " + prefix + " empty vault share clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, owner);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = owner,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tesSUCCESS));

                // Now the vault is empty, clawback again fails
                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = owner,
                        .amount = share(vaultShareBalance(vaultKeylet)).value(),
                    }),
                    Ter(tecNO_PERMISSION));
                env.close();
            }
        };

        Account const owner{"alice"};
        Account const depositor{"bob"};
        Account const issuer{"issuer"};

        env.fund(XRP(10000), issuer, owner, depositor);
        env.close();

        // Test XRP
        PrettyAsset const xrp = xrpIssue();
        testCase(xrp, "XRP", owner, depositor);
        testCase(xrp, "XRP (depositor is owner)", owner, owner);

        // Test IOU
        PrettyAsset const iou = issuer["IOU"];
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        env.trust(iou(1000), owner);
        env.trust(iou(1000), depositor);
        env(pay(issuer, owner, iou(100)));
        env(pay(issuer, depositor, iou(100)));
        env.close();
        testCase(iou, "IOU", owner, depositor);
        testCase(iou, "IOU (owner is issuer)", issuer, depositor);

        // Test MPT
        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        PrettyAsset const mpt = mptt.issuanceID();
        mptt.authorize({.account = owner});
        mptt.authorize({.account = depositor});
        env(pay(issuer, owner, mpt(1000)));
        env(pay(issuer, depositor, mpt(1000)));
        env.close();
        testCase(mpt, "MPT", owner, depositor);
        testCase(mpt, "MPT (owner is issuer)", issuer, depositor);
    }

    void
    testVaultClawbackAssets()
    {
        using namespace test::jtx;
        using namespace loan_broker;
        using namespace loan;
        Env env(*this);
        env.enableFeature(fixCleanup3_1_3);

        // Under featureLendingProtocolV1_1 LoanBrokerSet::preclaim only
        // accepts closed-ended vaults; some tests using this helper later
        // attach loan brokers to the vault. Build it as closed-ended and
        // advance past SubscriptionDate so subsequent broker/loan setup
        // runs in the Investment phase. VaultClawback itself is not
        // phase-gated. See the other setupVault (share tests) for why the
        // subscription offset must be generous.
        auto const setupVault = [&](PrettyAsset const& asset,
                                    Account const& owner,
                                    Account const& depositor,
                                    Account const& issuer) -> std::pair<Vault, Keylet> {
            Vault const vault{env};

            auto const& [tx, vaultKeylet, subscriptionDate] = vault.createClosedEnded(
                {.owner = owner, .asset = asset, .subscriptionOffset = std::chrono::seconds{60}});
            env(tx, Ter(tesSUCCESS));
            env.close();

            auto const& vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);
            env.memoize(Account("vault", vaultSle->at(sfAccount)));
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            vault.closePastSubscription(subscriptionDate);

            return std::make_pair(vault, vaultKeylet);
        };

        auto const testCase = [&](PrettyAsset const& asset,
                                  std::string const& prefix,
                                  Account const& owner,
                                  Account const& depositor,
                                  Account const& issuer) {
            if (asset.native())
            {
                testcase("VaultClawback (asset) - " + prefix + " issuer XRP clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);
                // If the asset is XRP, clawback with amount fails as malformed
                // when asset is specified.
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                        .amount = asset(1).value(),
                    }),
                    Ter(temMALFORMED));
                // When asset is implicit, clawback fails as no permission.
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                    }),
                    Ter(tecNO_PERMISSION));
                return;
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix + " clawback for different asset fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                Account const issuer2{"issuer2"};
                PrettyAsset const asset2 = issuer2["FOO"];
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset2(1).value(),
                    }),
                    Ter(tecWRONG_ASSET));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " ambiguous owner/issuer asset clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, issuer, depositor, issuer);
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                    }),
                    Ter(tecWRONG_ASSET));
            }

            {
                testcase("VaultClawback (asset) - " + prefix + " non-issuer asset clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tecNO_PERMISSION));

                env(vault.clawback({
                        .issuer = owner,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(1).value(),
                    }),
                    Ter(tecNO_PERMISSION));
            }

            {
                testcase("VaultClawback (asset) - " + prefix + " issuer clawback from self fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, issuer, issuer);
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = issuer,
                    }),
                    Ter(tecNO_PERMISSION));
            }

            {
                testcase("VaultClawback (asset) - " + prefix + " issuer share clawback fails");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);
                auto const& vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                Asset const share = vaultSle->at(sfShareMPTID);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = share(1).value(),
                    }),
                    Ter(tecNO_PERMISSION));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " partial issuer asset clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(1).value(),
                    }),
                    Ter(tesSUCCESS));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix + " full issuer asset clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(100).value(),
                    }),
                    Ter(tesSUCCESS));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " implicit full issuer asset clawback succeeds");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tesSUCCESS));
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " zero-amount clawback clamped with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;

                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                // Create a loan broker backed by this vault
                auto const brokerKeylet =
                    keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units, reducing assetsAvailable to 60
                // while assetsTotal stays at 100
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                // Zero-amount clawback (= "clawback all") should succeed,
                // clamped to assetsAvailable (60) rather than the full
                // share value (100).
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tesSUCCESS));
                env.close();

                // Only 60 assets clawed back; loan's 40 still outstanding
                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());

                    // 60 of 100 shares destroyed (1:1 ratio), 40 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{4, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " non-zero clawback clamped with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                // Create a loan broker backed by this vault
                auto const brokerKeylet =
                    keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                // Request 100 but only 60 available — clamped to 60
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(100).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());

                    // 60 of 100 shares destroyed (1:1 ratio), 40 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{4, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " partial clawback below available with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                // Create a loan broker backed by this vault
                auto const brokerKeylet =
                    keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units: assetsAvailable=60, assetsTotal=100
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                // Clawback 30 — well under available (60), no clamping needed
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(30).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(30).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(70).value());

                    // 30 of 100 shares destroyed (1:1 ratio), 70 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{7, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " clawback exactly equal to available with outstanding loan");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                auto const brokerKeylet =
                    keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows 40 units: assetsAvailable=60, assetsTotal=100
                env(set(depositor, brokerKeylet.key, asset(40).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                // Clawback exactly 60 — at the boundary, no clamping needed
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(60).value(),
                    }),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());

                    // 60 of 100 shares destroyed (1:1 ratio), 40 remain
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == shares(Number{4, sle->at(sfScale) + 1}));
                }
            }

            {
                testcase(
                    "VaultClawback (asset) - " + prefix +
                    " clawback with zero available (fully borrowed)");
                auto [vault, vaultKeylet] = setupVault(asset, owner, depositor, issuer);

                auto const vaultSle = env.le(vaultKeylet);
                if (!BEAST_EXPECT(vaultSle))
                    return;
                PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

                auto const brokerKeylet =
                    keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
                env(set(owner, vaultKeylet.key));
                env.close();

                // Depositor borrows all 100 units: assetsAvailable=0, assetsTotal=100
                env(set(depositor, brokerKeylet.key, asset(100).value()),
                    loan::kInterestRate(TenthBips32(0)),
                    kGracePeriod(60),
                    kPaymentInterval(120),
                    kPaymentTotal(10),
                    Sig(sfCounterpartySignature, owner),
                    Fee(env.current()->fees().base * 2),
                    Ter(tesSUCCESS));
                env.close();

                {
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                }

                auto const sharesBefore = env.balance(depositor, shares);

                // Zero-amount clawback — nothing available, clamped to 0,
                // resulting in zero shares destroyed → tecPRECISION_LOSS
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                    }),
                    Ter(tecPRECISION_LOSS));
                env.close();

                // Explicit amount clawback — also nothing available
                env(vault.clawback({
                        .issuer = issuer,
                        .id = vaultKeylet.key,
                        .holder = depositor,
                        .amount = asset(50).value(),
                    }),
                    Ter(tecPRECISION_LOSS));
                env.close();

                {
                    // Nothing changed — vault and shares unchanged
                    auto const sle = env.le(vaultKeylet);
                    BEAST_EXPECT(sle != nullptr);
                    BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(0).value());
                    BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(100).value());
                    auto const sharesAfter = env.balance(depositor, shares);
                    BEAST_EXPECT(sharesAfter == sharesBefore);
                }
            }
        };

        Account const owner{"alice"};
        Account const depositor{"bob"};
        Account const issuer{"issuer"};

        env.fund(XRP(10000), issuer, owner, depositor);
        env.close();

        // Test XRP
        PrettyAsset const xrp = xrpIssue();
        testCase(xrp, "XRP", owner, depositor, issuer);

        // Test IOU
        PrettyAsset const iou = issuer["IOU"];
        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();
        env.trust(iou(2000), owner);
        env.trust(iou(2000), depositor);
        env(pay(issuer, owner, iou(2000)));
        env(pay(issuer, depositor, iou(2000)));
        env.close();
        testCase(iou, "IOU", owner, depositor, issuer);

        // Test MPT
        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});

        PrettyAsset const mpt = mptt.issuanceID();
        mptt.authorize({.account = owner});
        mptt.authorize({.account = depositor});
        env(pay(issuer, depositor, mpt(2000)));
        env.close();
        testCase(mpt, "MPT", owner, depositor, issuer);

        // Test pre-fixCleanup3_1_3 legacy path: zero-amount clawback
        // returns early without clamping to assetsAvailable.
        {
            testcase(
                "VaultClawback (asset) - IOU pre-fixCleanup3_1_3"
                " zero-amount clawback unclamped with outstanding loan");

            env.disableFeature(fixCleanup3_1_3);

            auto [vault, vaultKeylet] = setupVault(iou, owner, depositor, issuer);

            auto const vaultSle = env.le(vaultKeylet);
            BEAST_EXPECT(vaultSle != nullptr);
            if (!vaultSle)
                return;

            PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

            // Create a loan broker backed by this vault
            auto const brokerKeylet =
                keylet::loanBroker(owner.id(), SeqProxy::rawSequence(env.seq(owner)));
            env(set(owner, vaultKeylet.key));
            env.close();

            // Depositor borrows 40 units, reducing assetsAvailable to 60
            // while assetsTotal stays at 100
            env(set(depositor, brokerKeylet.key, iou(40).value()),
                loan::kInterestRate(TenthBips32(0)),
                kGracePeriod(60),
                kPaymentInterval(120),
                kPaymentTotal(10),
                Sig(sfCounterpartySignature, owner),
                Fee(env.current()->fees().base * 2),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == iou(60).value());
                BEAST_EXPECT(sle->at(sfAssetsTotal) == iou(100).value());
            }

            auto const sharesBefore = env.balance(depositor, shares);

            // Legacy: zero-amount clawback tries to recover the full
            // share value (100) without clamping to assetsAvailable (60).
            // This causes the vault balance to go negative, triggering
            // the sanity check in doApply → tefINTERNAL.
            env(vault.clawback({
                    .issuer = issuer,
                    .id = vaultKeylet.key,
                    .holder = depositor,
                }),
                Ter(tefINTERNAL));
            env.close();

            {
                // Transaction rolled back — vault and shares unchanged
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle != nullptr);
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == iou(60).value());
                BEAST_EXPECT(sle->at(sfAssetsTotal) == iou(100).value());
                auto const sharesAfter = env.balance(depositor, shares);
                BEAST_EXPECT(sharesAfter == sharesBefore);
            }

            env.enableFeature(fixCleanup3_1_3);
        }
    }

    void
    testVaultEscrowedMPT()
    {
        using namespace test::jtx;
        using namespace std::literals;

        // Verify vault deposit/withdraw/clawback respect sfLockedAmount.
        // When MPT tokens are escrowed, sfMPTAmount is reduced and
        // sfLockedAmount is increased. Vault operations go through
        // accountSend/accountHolds which read sfMPTAmount, so escrowed
        // tokens are naturally excluded.

        {
            testcase("Vault deposit fails when MPT asset is escrowed");

            Env env{*this, testableAmendments()};
            auto const baseFee = env.current()->fees().base;
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const issuer{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(10000), issuer, owner, depositor, bob);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTCanEscrow});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            mptt.authorize({.account = bob});
            PrettyAsset const asset = mptt.issuanceID();
            env(pay(issuer, depositor, asset(100)));
            env.close();

            // Escrow 60 of 100 MPT tokens: sfMPTAmount drops to 40
            auto const escrowSeq = env.seq(depositor);
            env(escrow::create(depositor, bob, asset(60)),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            // Deposit 100 should fail — only 40 spendable
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // Deposit 40 (the unlocked balance) should succeed
            env(vault.deposit({.depositor = depositor, .id = vaultKeylet.key, .amount = asset(40)}),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(40).value());
            }

            // Clean up escrow
            env(escrow::finish(bob, depositor, escrowSeq),
                escrow::kCondition(escrow::kCb1),
                escrow::kFulfillment(escrow::kFb1),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();
        }

        {
            testcase("Vault withdraw respects escrowed shares");

            Env env{*this, testableAmendments()};
            auto const baseFee = env.current()->fees().base;
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const issuer{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(10000), issuer, owner, depositor, bob);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTCanEscrow});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            PrettyAsset const asset = mptt.issuanceID();
            env(pay(issuer, depositor, asset(100)));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            // Deposit 100 → get shares
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            auto const vaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            env.memoize(Account("vault", vaultSle->at(sfAccount)));
            PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

            // Authorize bob for share MPT so he can receive escrowed shares
            auto const shareMPTID = vaultSle->at(sfShareMPTID);
            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[sfMPTokenIssuanceID] = to_string(shareMPTID);
                jv[jss::TransactionType] = jss::MPTokenAuthorize;
                env(jv, Ter(tesSUCCESS));
                env.close();
            }

            // Escrow 60% of shares
            auto const escrowAmount = shares(Number{6, vaultSle->at(sfScale) + 1});
            env(escrow::create(depositor, bob, escrowAmount),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            // Withdraw all 100 should fail — only 40% of shares are unlocked
            env(vault.withdraw(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tecINSUFFICIENT_FUNDS));
            env.close();

            // Withdraw 40 (matching unlocked shares) should succeed
            env(vault.withdraw(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(40)}),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(60).value());
            }
        }

        {
            testcase("Vault clawback only recovers unlocked shares");

            Env env{*this, testableAmendments() | fixCleanup3_1_3};
            auto const baseFee = env.current()->fees().base;
            Account const owner{"owner"};
            Account const depositor{"depositor"};
            Account const issuer{"issuer"};
            Account const bob{"bob"};

            env.fund(XRP(10000), issuer, owner, depositor, bob);
            env.close();

            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock | tfMPTCanEscrow});
            mptt.authorize({.account = owner});
            mptt.authorize({.account = depositor});
            PrettyAsset const asset = mptt.issuanceID();
            env(pay(issuer, depositor, asset(100)));
            env.close();

            Vault const vault{env};
            auto [tx, vaultKeylet] = vault.create({.owner = owner, .asset = asset});
            env(tx, Ter(tesSUCCESS));
            env.close();

            // Deposit 100 → get shares
            env(vault.deposit(
                    {.depositor = depositor, .id = vaultKeylet.key, .amount = asset(100)}),
                Ter(tesSUCCESS));
            env.close();

            auto const vaultSle = env.le(vaultKeylet);
            if (!BEAST_EXPECT(vaultSle))
                return;
            env.memoize(Account("vault", vaultSle->at(sfAccount)));
            PrettyAsset const shares = MPTIssue(vaultSle->at(sfShareMPTID));

            // Authorize bob for share MPT so he can receive escrowed shares
            auto const shareMPTID = vaultSle->at(sfShareMPTID);
            {
                json::Value jv;
                jv[jss::Account] = bob.human();
                jv[sfMPTokenIssuanceID] = to_string(shareMPTID);
                jv[jss::TransactionType] = jss::MPTokenAuthorize;
                env(jv, Ter(tesSUCCESS));
                env.close();
            }

            // Escrow 60% of shares
            auto const escrowAmount = shares(Number{6, vaultSle->at(sfScale) + 1});
            env(escrow::create(depositor, bob, escrowAmount),
                escrow::kCondition(escrow::kCb1),
                escrow::kFinishTime(env.now() + 1s),
                Fee(baseFee * 150),
                Ter(tesSUCCESS));
            env.close();

            // Zero-amount clawback ("all") — should only recover assets
            // corresponding to unlocked shares (40%)
            env(vault.clawback({
                    .issuer = issuer,
                    .id = vaultKeylet.key,
                    .holder = depositor,
                }),
                Ter(tesSUCCESS));
            env.close();

            {
                auto const sle = env.le(vaultKeylet);
                BEAST_EXPECT(sle != nullptr);
                // Only 40 of 100 assets recovered (matching 40% unlocked shares)
                BEAST_EXPECT(sle->at(sfAssetsTotal) == asset(60).value());
                BEAST_EXPECT(sle->at(sfAssetsAvailable) == asset(60).value());

                // Depositor's unlocked shares are now 0
                auto const sharesAfter = env.balance(depositor, shares);
                BEAST_EXPECT(sharesAfter == shares(0));
            }
        }
    }

public:
    void
    run() override
    {
        testVaultClawbackBurnShares();
        testVaultClawbackAssets();
        testVaultEscrowedMPT();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(VaultClawback, app, xrpl, 1);

}  // namespace xrpl
