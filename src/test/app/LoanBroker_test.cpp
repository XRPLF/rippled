
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/fee.h>
#include <test/jtx/flags.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/sig.h>
#include <test/jtx/tag.h>
#include <test/jtx/tags.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/core/ServiceRegistry.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/Units.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/Transactor.h>
#include <xrpl/tx/transactors/lending/LoanBrokerCoverDeposit.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <tuple>
#include <vector>

namespace xrpl::test {

class LoanBroker_test : public beast::unit_test::Suite
{
    // Ensure that all the features needed for Lending Protocol are included,
    // even if they are set to unsupported.
    FeatureBitset const all_{jtx::testableAmendments()};

    void
    testDisabled()
    {
        testcase("Disabled");
        // Lending Protocol depends on Single Asset Vault (SAV). Test
        // combinations of the two amendments.
        // Single Asset Vault depends on MPTokensV1, but don't test every combo
        // of that.
        using namespace jtx;
        auto failAll = [this](FeatureBitset features, bool goodVault = false) {
            Env env(*this, features);

            Account const alice{"alice"};
            env.fund(XRP(10000), alice);

            // Try to create a vault
            PrettyAsset const asset{xrpIssue(), 1'000'000};
            Vault const vault{env};
            auto const [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx, Ter(goodVault ? Ter(tesSUCCESS) : Ter(temDISABLED)));
            env.close();
            BEAST_EXPECT(static_cast<bool>(env.le(keylet)) == goodVault);

            using namespace loanBroker;
            // Can't create a loan broker regardless of whether the vault exists
            env(set(alice, keylet.key), Ter(temDISABLED));
            auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice));
            // Other LoanBroker transactions are disabled, too.
            // 1. LoanBrokerCoverDeposit
            env(coverDeposit(alice, brokerKeylet.key, asset(1000)), Ter(temDISABLED));
            // 2. LoanBrokerCoverWithdraw
            env(coverWithdraw(alice, brokerKeylet.key, asset(1000)), Ter(temDISABLED));
            // 3. LoanBrokerCoverClawback
            env(coverClawback(alice), Ter(temDISABLED));
            env(coverClawback(alice), kLoanBrokerId(brokerKeylet.key), Ter(temDISABLED));
            env(coverClawback(alice), kAmount(asset(0)), Ter(temDISABLED));
            env(coverClawback(alice),
                kLoanBrokerId(brokerKeylet.key),
                kAmount(asset(1000)),
                Ter(temDISABLED));
            // 4. LoanBrokerDelete
            env(del(alice, brokerKeylet.key), Ter(temDISABLED));
        };
        failAll(all_ - featureMPTokensV1);
        failAll(all_ - featureSingleAssetVault - featureLendingProtocol);
        failAll(all_ - featureSingleAssetVault);
        failAll(all_ - featureLendingProtocol, true);
    }

    struct VaultInfo
    {
        jtx::PrettyAsset asset;
        uint256 vaultID;
        jtx::Account pseudoAccount;
        VaultInfo(jtx::PrettyAsset const& asset, uint256 const& vaultId, AccountID const& pseudo)
            : asset(asset), vaultID(vaultId), pseudoAccount("vault", pseudo)
        {
        }
    };

    void
    lifecycle(
        char const* label,
        jtx::Env& env,
        jtx::Account const& issuer,
        jtx::Account const& alice,
        jtx::Account const& evan,
        jtx::Account const& bystander,
        VaultInfo const& vault,
        VaultInfo const& badVault,
        std::function<jtx::JTx(jtx::JTx const&)> modifyJTx,
        std::function<void(SLE::const_ref)> checkBroker,
        std::function<void(SLE::const_ref)> changeBroker,
        std::function<void(SLE::const_ref)> checkChangedBroker)
    {
        {
            auto const& asset = vault.asset.raw();
            std::string_view assetLabel;
            if (asset.native())
            {
                assetLabel = "XRP ";
            }
            else if (asset.holds<Issue>())
            {
                assetLabel = "IOU ";
            }
            else if (asset.holds<MPTIssue>())
            {
                assetLabel = "MPT ";
            }
            else
            {
                assetLabel = "Unknown ";
            }
            testcase << "Lifecycle: " << assetLabel << label;
        }

        using namespace jtx;
        using namespace loanBroker;

        // Bogus assets to use in test cases
        static PrettyAsset const kBadMptAsset = [&]() {
            MPTTester badMptt{env, evan, kMptInitNoFund};
            badMptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            env.close();
            return badMptt["BAD"];
        }();
        static PrettyAsset const kBadIouAsset = evan["BAD"];
        static Account const kNonExistent{"NonExistent"};
        static PrettyAsset const kGhostIouAsset = kNonExistent["GST"];
        PrettyAsset const vaultPseudoIouAsset = vault.pseudoAccount["PSD"];

        auto const badKeylet = keylet::loanbroker(alice.id(), env.seq(alice));
        env(set(alice, badVault.vaultID));
        env.close();
        auto const badBrokerPseudo = [&]() {
            if (auto const le = env.le(badKeylet); BEAST_EXPECT(le))
            {
                return Account{"Bad Broker pseudo-account", le->at(sfAccount)};
            }
            // Just to make the build work
            return vault.pseudoAccount;
        }();
        PrettyAsset const badBrokerPseudoIouAsset = badBrokerPseudo["WAT"];

        auto const keylet = keylet::loanbroker(alice.id(), env.seq(alice));
        {
            // Start with default values
            auto jtx = env.jt(set(alice, vault.vaultID));
            // Modify as desired
            if (modifyJTx)
                jtx = modifyJTx(jtx);
            // Successfully create a Loan Broker
            env(jtx);
        }

        env.close();
        if (auto broker = env.le(keylet); BEAST_EXPECT(broker))
        {
            // log << "Broker after create: " << to_string(broker->getJson())
            //     << std::endl;
            BEAST_EXPECT(broker->at(sfVaultID) == vault.vaultID);
            BEAST_EXPECT(broker->at(sfAccount) != alice.id());
            BEAST_EXPECT(broker->at(sfOwner) == alice.id());
            BEAST_EXPECT(broker->at(sfFlags) == 0);
            BEAST_EXPECT(broker->at(sfSequence) == env.seq(alice) - 1);
            BEAST_EXPECT(broker->at(sfOwnerCount) == 0);
            BEAST_EXPECT(broker->at(sfLoanSequence) == 1);
            BEAST_EXPECT(broker->at(sfDebtTotal) == 0);
            BEAST_EXPECT(broker->at(sfCoverAvailable) == 0);
            if (checkBroker)
                checkBroker(broker);

            // if (auto const vaultSLE = env.le(keylet::vault(vault.vaultID)))
            //{
            //     log << "Vault: " << to_string(vaultSLE->getJson()) <<
            //     std::endl;
            // }
            //  Load the pseudo-account
            Account const pseudoAccount{"Broker pseudo-account", broker->at(sfAccount)};

            auto const pseudoKeylet = keylet::account(pseudoAccount);
            if (auto const pseudo = env.le(pseudoKeylet); BEAST_EXPECT(pseudo))
            {
                // log << "Pseudo-account after create: "
                //     << to_string(pseudo->getJson()) << std::endl
                //     << std::endl;
                BEAST_EXPECT(
                    pseudo->at(sfFlags) == (lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth));
                BEAST_EXPECT(pseudo->at(sfSequence) == 0);
                BEAST_EXPECT(pseudo->at(sfBalance) == beast::kZero);
                BEAST_EXPECT(pseudo->at(sfOwnerCount) == (vault.asset.raw().native() ? 0 : 1));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfAccountTxnID));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfRegularKey));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfEmailHash));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfWalletLocator));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfWalletSize));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfMessageKey));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfTransferRate));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfDomain));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfTickSize));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfTicketCount));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfNFTokenMinter));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfMintedNFTokens));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfBurnedNFTokens));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfFirstNFTokenSequence));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfAMMID));
                BEAST_EXPECT(!pseudo->isFieldPresent(sfVaultID));
                BEAST_EXPECT(pseudo->at(sfLoanBrokerID) == keylet.key);
            }

            {
                // Get the AccountInfo RPC result for the broker pseudo-account
                std::string const pseudoStr = to_string(pseudoAccount.id());
                auto const accountInfo = env.rpc("account_info", pseudoStr);
                if (BEAST_EXPECT(accountInfo.isObject()))
                {
                    auto const& accountData = accountInfo[jss::result][jss::account_data];
                    if (BEAST_EXPECT(accountData.isObject()))
                    {
                        BEAST_EXPECT(accountData[jss::Account] == pseudoStr);
                        BEAST_EXPECT(accountData[sfLoanBrokerID] == to_string(keylet.key));
                    }
                    auto const& pseudoInfo = accountInfo[jss::result][jss::pseudo_account];
                    if (BEAST_EXPECT(pseudoInfo.isObject()))
                    {
                        BEAST_EXPECT(pseudoInfo[jss::type] == "LoanBroker");
                    }
                }
            }

            auto verifyCoverAmount =
                [&env, &vault, &pseudoAccount, &broker, &keylet, this](auto n) {
                    using namespace jtx;

                    if (BEAST_EXPECT(broker = env.le(keylet)))
                    {
                        auto const amount = vault.asset(n);
                        BEAST_EXPECT(broker->at(sfCoverAvailable) == amount.number());
                        env.require(Balance(pseudoAccount, amount));
                    }
                };

            // Test Cover funding before allowing alterations
            env(coverDeposit(alice, uint256(0), vault.asset(10)), Ter(temINVALID));
            env(coverDeposit(evan, keylet.key, vault.asset(10)), Ter(tecNO_PERMISSION));
            env(coverDeposit(evan, keylet.key, vault.asset(0)), Ter(temBAD_AMOUNT));
            env(coverDeposit(evan, keylet.key, vault.asset(-10)), Ter(temBAD_AMOUNT));
            env(coverDeposit(alice, vault.vaultID, vault.asset(10)), Ter(tecNO_ENTRY));

            verifyCoverAmount(0);

            // Test cover clawback failure cases BEFORE depositing any cover
            // Need one of brokerID or amount
            env(coverClawback(alice), Ter(temINVALID));
            env(coverClawback(alice), kLoanBrokerId(uint256(0)), Ter(temINVALID));
            env(coverClawback(alice), kAmount(XRP(1000)), Ter(temBAD_AMOUNT));
            env(coverClawback(alice), kAmount(vault.asset(-10)), Ter(temBAD_AMOUNT));
            // Clawbacks with an MPT need to specify the broker ID
            env(coverClawback(alice), kAmount(kBadMptAsset(1)), Ter(temINVALID));
            env(coverClawback(evan), kLoanBrokerId(vault.vaultID), Ter(tecNO_ENTRY));
            // Only the issuer can clawback
            env(coverClawback(alice), kLoanBrokerId(keylet.key), Ter(tecNO_PERMISSION));
            if (vault.asset.raw().native())
            {
                // Can not clawback XRP under any circumstances
                env(coverClawback(issuer), kLoanBrokerId(keylet.key), Ter(tecNO_PERMISSION));
            }
            else
            {
                if (vault.asset.raw().holds<Issue>())
                {
                    // Clawbacks without a kLoanBrokerId need to specify an IOU
                    // with the broker's pseudo-account as the issuer
                    env(coverClawback(alice), kAmount(kGhostIouAsset(1)), Ter(tecNO_ENTRY));
                    env(coverClawback(alice), kAmount(kBadIouAsset(1)), Ter(tecOBJECT_NOT_FOUND));
                    // Pseudo-account is not for a broker
                    env(coverClawback(alice),
                        kAmount(vaultPseudoIouAsset(1)),
                        Ter(tecOBJECT_NOT_FOUND));
                    // If we specify a pseudo-account as the IOU amount, it
                    // needs to match the loan broker
                    env(coverClawback(issuer),
                        kLoanBrokerId(keylet.key),
                        kAmount(badBrokerPseudoIouAsset(10)),
                        Ter(tecWRONG_ASSET));
                    PrettyAsset const brokerWrongCurrencyAsset = pseudoAccount["WAT"];
                    env(coverClawback(issuer),
                        kLoanBrokerId(keylet.key),
                        kAmount(brokerWrongCurrencyAsset(10)),
                        Ter(tecWRONG_ASSET));
                }
                else
                {
                    // Clawbacks with an MPT need to specify the broker ID, even
                    // if the asset is valid
                    BEAST_EXPECT(vault.asset.raw().holds<MPTIssue>());
                    env(coverClawback(alice), kAmount(vault.asset(10)), Ter(temINVALID));
                }
                // Since no cover has been deposited, there's nothing to claw
                // back
                env(coverClawback(issuer),
                    kLoanBrokerId(keylet.key),
                    kAmount(vault.asset(10)),
                    Ter(tecINSUFFICIENT_FUNDS));
            }
            env.close();

            // Fund the cover deposit
            env(coverDeposit(alice, keylet.key, vault.asset(10)));
            env.close();
            verifyCoverAmount(10);

            // Test withdrawal failure cases
            env(coverWithdraw(alice, uint256(0), vault.asset(10)), Ter(temINVALID));
            env(coverWithdraw(evan, keylet.key, vault.asset(10)), Ter(tecNO_PERMISSION));
            env(coverWithdraw(evan, keylet.key, vault.asset(0)), Ter(temBAD_AMOUNT));
            env(coverWithdraw(evan, keylet.key, vault.asset(-10)), Ter(temBAD_AMOUNT));
            env(coverWithdraw(alice, vault.vaultID, vault.asset(10)), Ter(tecNO_ENTRY));
            env(coverWithdraw(alice, keylet.key, vault.asset(900)), Ter(tecINSUFFICIENT_FUNDS));

            // Skip this test for XRP, because that can always be sent
            if (!vault.asset.raw().native())
            {
                TER const expected = vault.asset.raw().holds<MPTIssue>() ? tecNO_AUTH : tecNO_LINE;
                env(coverWithdraw(alice, keylet.key, vault.asset(1)),
                    kDestination(bystander),
                    Ter(expected));
            }

            // Can not withdraw to the zero address
            env(coverWithdraw(alice, keylet.key, vault.asset(1)),
                kDestination(AccountID{}),
                Ter(temMALFORMED));

            // Withdraw some of the cover amount
            env(coverWithdraw(alice, keylet.key, vault.asset(7)));
            env.close();
            verifyCoverAmount(3);

            // Add some more cover
            env(coverDeposit(alice, keylet.key, vault.asset(5)));
            env.close();
            verifyCoverAmount(8);

            // Withdraw some more. Send it to Evan. Very generous, considering
            // how much trouble he's been.
            env(coverWithdraw(alice, keylet.key, vault.asset(1)), kDestination(evan));
            env.close();
            verifyCoverAmount(7);

            // Withdraw some more. Send it to Evan. Very generous, considering
            // how much trouble he's been.
            env(coverWithdraw(alice, keylet.key, vault.asset(1)), kDestination(evan), Dtag(3));
            env.close();
            verifyCoverAmount(6);

            if (!vault.asset.raw().native())
            {
                // Issuer claws back some of the cover
                env(coverClawback(issuer), kLoanBrokerId(keylet.key), kAmount(vault.asset(2)));
                env.close();
                verifyCoverAmount(4);

                // Deposit some back
                env(coverDeposit(alice, keylet.key, vault.asset(5)));
                env.close();
                verifyCoverAmount(9);

                // Issuer claws it all back in various different ways
                for (auto const& tx : {
                         // defer autofills until submission time
                         env.json(
                             coverClawback(issuer),
                             kLoanBrokerId(keylet.key),
                             Fee(kNone),
                             Seq(kNone),
                             Sig(kNone)),
                         env.json(
                             coverClawback(issuer),
                             kLoanBrokerId(keylet.key),
                             kAmount(vault.asset(0)),
                             Fee(kNone),
                             Seq(kNone),
                             Sig(kNone)),
                         env.json(
                             coverClawback(issuer),
                             kLoanBrokerId(keylet.key),
                             kAmount(vault.asset(6)),
                             Fee(kNone),
                             Seq(kNone),
                             Sig(kNone)),
                         // amount will be truncated to what's available
                         env.json(
                             coverClawback(issuer),
                             kLoanBrokerId(keylet.key),
                             kAmount(vault.asset(100)),
                             Fee(kNone),
                             Seq(kNone),
                             Sig(kNone)),
                     })
                {
                    // Issuer claws it all back
                    env(tx);
                    env.close();
                    verifyCoverAmount(0);

                    // Deposit some back
                    env(coverDeposit(alice, keylet.key, vault.asset(6)));
                    env.close();
                    verifyCoverAmount(6);
                }
            }

            // no-op
            env(set(alice, vault.vaultID), kLoanBrokerId(keylet.key));
            env.close();

            // Make modifications to the broker
            if (changeBroker)
                changeBroker(broker);

            env.close();

            // Check the results of modifications
            if (BEAST_EXPECT(broker = env.le(keylet)) && checkChangedBroker)
                checkChangedBroker(broker);

            // Verify that fields get removed when set to default values
            // Debt maximum: explicit 0
            // Data: explicit empty
            env(set(alice, vault.vaultID),
                kLoanBrokerId(broker->key()),
                kDebtMaximum(Number(0)),
                kData(""));
            env.close();

            // Check the updated fields
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                BEAST_EXPECT(!broker->isFieldPresent(sfData));
            }

            /////////////////////////////////////
            // try to delete the wrong broker object
            env(del(alice, vault.vaultID), Ter(tecNO_ENTRY));
            // evan tries to delete the broker
            env(del(evan, keylet.key), Ter(tecNO_PERMISSION));

            // Get the "bad" broker out of the way
            env(del(alice, badKeylet.key));
            env.close();

            // Note alice's balance of the asset and the broker account's cover
            // funds
            auto const aliceBalance = env.balance(alice, vault.asset);
            auto const coverFunds = env.balance(pseudoAccount, vault.asset);
            BEAST_EXPECT(coverFunds.number() == broker->at(sfCoverAvailable));
            BEAST_EXPECT(coverFunds != beast::kZero);
            verifyCoverAmount(6);

            // delete the broker
            // log << "Broker before delete: " << to_string(broker->getJson())
            //    << std::endl;
            // if (auto const pseudo = env.le(pseudoKeylet);
            // BEAST_EXPECT(pseudo))
            //{
            //    log << "Pseudo-account before delete: "
            //        << to_string(pseudo->getJson()) << std::endl
            //        << std::endl;
            //}

            env(del(alice, keylet.key));
            env.close();
            {
                broker = env.le(keylet);
                BEAST_EXPECT(!broker);
                auto pseudo = env.le(pseudoKeylet);
                BEAST_EXPECT(!pseudo);
            }
            auto const expectedBalance = aliceBalance + coverFunds -
                (aliceBalance.value().native() ? STAmount(env.current()->fees().base.value())
                                               : vault.asset(0));
            env.require(Balance(alice, expectedBalance));
            env.require(Balance(pseudoAccount, vault.asset(kNone)));
        }
    }

    void
    testLifecycle()
    {
        testcase("Lifecycle");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for an
        // MPT. That'll require three corresponding SAVs.
        Env env(*this, all_);

        Account const issuer{"issuer"};
        // For simplicity, alice will be the sole actor for the vault & brokers.
        Account const alice{"alice"};
        // Evan will attempt to be naughty
        Account const evan{"evan"};
        // Bystander doesn't have anything to do with the SAV or Broker, or any
        // of the relevant tokens
        Account const bystander{"bystander"};
        Vault vault{env};

        // Fund the accounts and trust lines with the same amount so that tests
        // can use the same values regardless of the asset.
        env.fund(XRP(100'000), issuer, noripple(alice, evan, bystander));
        env.close();

        env(fset(issuer, asfAllowTrustLineClawback));
        env.close();

        // Create assets
        PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};
        PrettyAsset const iouAsset = issuer["IOU"];
        env(trust(alice, iouAsset(1'000'000)));
        env(trust(evan, iouAsset(1'000'000)));
        env.close();
        env(pay(issuer, evan, iouAsset(100'000)));
        env(pay(issuer, alice, iouAsset(100'000)));
        env.close();

        MPTTester mptt{env, issuer, kMptInitNoFund};
        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
        env.close();
        PrettyAsset const mptAsset = mptt["MPT"];
        mptt.authorize({.account = alice});
        mptt.authorize({.account = evan});
        env.close();
        env(pay(issuer, alice, mptAsset(100'000)));
        env(pay(issuer, evan, mptAsset(100'000)));
        env.close();

        std::array const assets{xrpAsset, iouAsset, mptAsset};

        // Create vaults
        std::vector<VaultInfo> vaults;
        for (auto const& asset : assets)
        {
            auto [tx, keylet] = vault.create({.owner = alice, .asset = asset});
            env(tx);
            env.close();
            if (auto const le = env.le(keylet); BEAST_EXPECT(env.le(keylet)))
            {
                vaults.emplace_back(asset, keylet.key, le->at(sfAccount));
            }

            env(vault.deposit({.depositor = alice, .id = keylet.key, .amount = asset(50)}));
            env.close();
        }
        VaultInfo const badVault = [&]() -> VaultInfo {
            auto [tx, keylet] = vault.create({.owner = alice, .asset = iouAsset});
            env(tx);
            env.close();
            if (auto const le = env.le(keylet); BEAST_EXPECT(env.le(keylet)))
            {
                return {iouAsset, keylet.key, le->at(sfAccount)};
            }
            // This should never happen
            return {iouAsset, keylet.key, evan.id()};
        }();

        auto const aliceOriginalCount = env.ownerCount(alice);

        // Create and update Loan Brokers
        for (auto const& vault : vaults)
        {
            {
                // Get the AccountInfo RPC result for the vault pseudo-account
                std::string const pseudoStr = to_string(vault.pseudoAccount.id());
                auto const accountInfo = env.rpc("account_info", pseudoStr);
                if (BEAST_EXPECT(accountInfo.isObject()))
                {
                    auto const& accountData = accountInfo[jss::result][jss::account_data];
                    if (BEAST_EXPECT(accountData.isObject()))
                    {
                        BEAST_EXPECT(accountData[jss::Account] == pseudoStr);
                        BEAST_EXPECT(accountData[sfVaultID] == to_string(vault.vaultID));
                    }
                    auto const& pseudoInfo = accountInfo[jss::result][jss::pseudo_account];
                    if (BEAST_EXPECT(pseudoInfo.isObject()))
                    {
                        BEAST_EXPECT(pseudoInfo[jss::type] == "Vault");
                    }
                }
            }

            using namespace loanBroker;
            using namespace xrpl::Lending;

            TenthBips32 const tenthBipsZero{0};

            auto badKeylet = keylet::vault(alice.id(), env.seq(alice));
            // Try some failure cases
            // not the vault owner
            env(set(evan, vault.vaultID), Ter(tecNO_PERMISSION));
            // not a vault
            env(set(alice, badKeylet.key), Ter(tecNO_ENTRY));
            // flags are checked first
            env(set(evan, vault.vaultID, ~tfUniversal), Ter(temINVALID_FLAG));
            // field length validation
            // sfData: good length, bad account
            env(set(evan, vault.vaultID),
                kData(std::string(kMaxDataPayloadLength, 'X')),
                Ter(tecNO_PERMISSION));
            // sfData: too long
            env(set(evan, vault.vaultID),
                kData(std::string(kMaxDataPayloadLength + 1, 'Y')),
                Ter(temINVALID));
            // sfManagementFeeRate: good value, bad account
            env(set(evan, vault.vaultID),
                kManagementFeeRate(kMaxManagementFeeRate),
                Ter(tecNO_PERMISSION));
            // sfManagementFeeRate: too big
            env(set(evan, vault.vaultID),
                kManagementFeeRate(kMaxManagementFeeRate + TenthBips16(10)),
                Ter(temINVALID));
            // sfCoverRateMinimum and sfCoverRateLiquidation are linked
            // Cover: good value, bad account
            env(set(evan, vault.vaultID),
                kCoverRateMinimum(kMaxCoverRate),
                kCoverRateLiquidation(kMaxCoverRate),
                Ter(tecNO_PERMISSION));
            // CoverMinimum: too big
            env(set(evan, vault.vaultID),
                kCoverRateMinimum(kMaxCoverRate + 1),
                kCoverRateLiquidation(kMaxCoverRate + 1),
                Ter(temINVALID));
            // CoverLiquidation: too big
            env(set(evan, vault.vaultID),
                kCoverRateMinimum(kMaxCoverRate / 2),
                kCoverRateLiquidation(kMaxCoverRate + 1),
                Ter(temINVALID));
            // Cover: zero min, non-zero liquidation - implicit and
            // explicit zero values.
            env(set(evan, vault.vaultID), kCoverRateLiquidation(kMaxCoverRate), Ter(temINVALID));
            env(set(evan, vault.vaultID),
                kCoverRateMinimum(tenthBipsZero),
                kCoverRateLiquidation(kMaxCoverRate),
                Ter(temINVALID));
            // Cover: non-zero min, zero liquidation - implicit and
            // explicit zero values.
            env(set(evan, vault.vaultID), kCoverRateMinimum(kMaxCoverRate), Ter(temINVALID));
            env(set(evan, vault.vaultID),
                kCoverRateMinimum(kMaxCoverRate),
                kCoverRateLiquidation(tenthBipsZero),
                Ter(temINVALID));
            // sfDebtMaximum: good value, bad account
            env(set(evan, vault.vaultID), kDebtMaximum(Number(0)), Ter(tecNO_PERMISSION));
            // sfDebtMaximum: overflow
            env(set(evan, vault.vaultID), kDebtMaximum(Number(1, 100)), Ter(temINVALID));
            // sfDebtMaximum: negative
            env(set(evan, vault.vaultID), kDebtMaximum(Number(-1)), Ter(temINVALID));

            std::string testData;
            lifecycle(
                "default fields",
                env,
                issuer,
                alice,
                evan,
                bystander,
                vault,
                badVault,
                // No modifications
                {},
                [&](SLE::const_ref broker) {
                    // Extra checks
                    BEAST_EXPECT(!broker->isFieldPresent(sfManagementFeeRate));
                    BEAST_EXPECT(!broker->isFieldPresent(sfCoverRateMinimum));
                    BEAST_EXPECT(!broker->isFieldPresent(sfCoverRateLiquidation));
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 0);

                    BEAST_EXPECT(env.ownerCount(alice) == aliceOriginalCount + 4);
                },
                [&](SLE::const_ref broker) {
                    // Modifications

                    // Update the fields
                    auto const nextKeylet = keylet::loanbroker(alice.id(), env.seq(alice));

                    // fields that can't be changed
                    // LoanBrokerID
                    env(set(alice, vault.vaultID), kLoanBrokerId(nextKeylet.key), Ter(tecNO_ENTRY));
                    // VaultID
                    env(set(alice, nextKeylet.key), kLoanBrokerId(broker->key()), Ter(tecNO_ENTRY));
                    // Owner
                    env(set(evan, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        Ter(tecNO_PERMISSION));
                    // ManagementFeeRate
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kManagementFeeRate(kMaxManagementFeeRate),
                        Ter(temINVALID));
                    // CoverRateMinimum
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kCoverRateMinimum(kMaxManagementFeeRate),
                        Ter(temINVALID));
                    // CoverRateLiquidation
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kCoverRateLiquidation(kMaxManagementFeeRate),
                        Ter(temINVALID));

                    // fields that can be changed
                    testData = "Test Data 1234";
                    // Bad data: too long
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kData(std::string(kMaxDataPayloadLength + 1, 'W')),
                        Ter(temINVALID));

                    // Bad debt maximum
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kDebtMaximum(Number(-175, -1)),
                        Ter(temINVALID));
                    Number debtMax{175, -1};
                    if (vault.asset.integral())
                    {
                        env(set(alice, vault.vaultID),
                            kLoanBrokerId(broker->key()),
                            kData(testData),
                            kDebtMaximum(debtMax),
                            Ter(tecPRECISION_LOSS));
                        roundToAsset(vault.asset, debtMax);
                    }
                    // Data & Debt maximum
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kData(testData),
                        kDebtMaximum(debtMax));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData));
                    Number const expected = STAmount{vault.asset, Number(175, -1)};
                    auto const actual = broker->at(sfDebtMaximum);
                    BEAST_EXPECTS(
                        actual == expected,
                        "Expected: " + to_string(expected) + ", Actual: " + to_string(actual));
                });

            lifecycle(
                "non-default fields",
                env,
                issuer,
                alice,
                evan,
                bystander,
                vault,
                badVault,
                [&](jtx::JTx const& jv) {
                    testData = "spam spam spam spam";
                    // Finally, create another Loan Broker with kNone of the
                    // values at default
                    return env.jt(
                        jv,
                        kData(testData),
                        kManagementFeeRate(TenthBips16(123)),
                        kDebtMaximum(Number(9)),
                        kCoverRateMinimum(TenthBips32(100)),
                        kCoverRateLiquidation(TenthBips32(200)));
                },
                [&](SLE::const_ref broker) {
                    // Extra checks
                    BEAST_EXPECT(broker->at(sfManagementFeeRate) == 123);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 100);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 200);
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == Number(9));
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData));
                },
                [&](SLE::const_ref broker) {
                    // Reset Data & Debt maximum to default values
                    env(set(alice, vault.vaultID),
                        kLoanBrokerId(broker->key()),
                        kData(""),
                        kDebtMaximum(Number(0)));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                });
        }

        BEAST_EXPECT(env.ownerCount(alice) == aliceOriginalCount);
    }

    enum class LoanBrokerTest { CoverClawback, CoverDeposit, CoverWithdraw, Delete, Set };

    void
    testLoanBroker(
        std::function<jtx::PrettyAsset(jtx::Env&, jtx::Account const&, jtx::Account const&)>
            getAsset,
        LoanBrokerTest brokerTest)
    {
        using namespace jtx;
        using namespace loanBroker;
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Env env(*this);
        Vault const vault{env};

        env.fund(XRP(100'000), issuer, alice);
        env.close();

        PrettyAsset const asset = [&]() {
            if (getAsset)
                return getAsset(env, issuer, alice);
            env(trust(alice, issuer["IOU"](1'000'000)));
            env.close();
            return PrettyAsset(issuer["IOU"]);
        }();

        env(pay(issuer, alice, asset(100'000)));
        env.close();

        auto [tx, vaultKeylet] = vault.create({.owner = alice, .asset = asset});
        env(tx);
        env.close();
        auto const le = env.le(vaultKeylet);
        VaultInfo vaultInfo = [&]() {
            if (BEAST_EXPECT(le))
                return VaultInfo{asset, vaultKeylet.key, le->at(sfAccount)};
            return VaultInfo{asset, {}, {}};
        }();
        if (vaultInfo.vaultID == uint256{})
            return;

        env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = asset(50)}));
        env.close();

        auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice));
        env(set(alice, vaultInfo.vaultID));
        env.close();

        auto broker = env.le(brokerKeylet);
        if (!BEAST_EXPECT(broker))
            return;

        auto testZeroBrokerID = [&](auto&& getTxJv) {
            auto jv = getTxJv();
            // empty broker ID
            jv[sfLoanBrokerID] = "";
            env(jv, Ter(temINVALID));
            // zero broker ID
            jv[sfLoanBrokerID] = to_string(uint256{});
            // needs a flag to distinguish the parsed STTx from the prior
            // test
            env(jv, Txflags(tfFullyCanonicalSig), Ter(temINVALID));
        };
        auto testZeroVaultID = [&](auto&& getTxJv) {
            auto jv = getTxJv();
            // empty broker ID
            jv[sfVaultID] = "";
            env(jv, Ter(temINVALID));
            // zero broker ID
            jv[sfVaultID] = to_string(uint256{});
            // needs a flag to distinguish the parsed STTx from the prior
            // test
            env(jv, Txflags(tfFullyCanonicalSig), Ter(temINVALID));
        };

        if (brokerTest == LoanBrokerTest::CoverDeposit)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() { return coverDeposit(alice, brokerKeylet.key, asset(10)); });

            // preclaim: tecWRONG_ASSET
            env(coverDeposit(alice, brokerKeylet.key, issuer["BAD"](10)), Ter(tecWRONG_ASSET));

            // preclaim: tecINSUFFICIENT_FUNDS
            env(pay(alice, issuer, asset(100'000 - 50)));
            env.close();
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)),
                Ter(tecINSUFFICIENT_FUNDS));

            // preclaim: tecFROZEN
            env(fset(issuer, asfGlobalFreeze));
            env.close();
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)), Ter(tecFROZEN));
        }
        else
        {
            // Fund the cover deposit
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)));
        }
        env.close();

        if (brokerTest == LoanBrokerTest::CoverWithdraw)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() { return coverWithdraw(alice, brokerKeylet.key, asset(10)); });

            // preclaim: tecWRONG_ASSET
            env(coverWithdraw(alice, brokerKeylet.key, issuer["BAD"](10)), Ter(tecWRONG_ASSET));

            // preclaim: tecNO_DST
            Account const bogus{"bogus"};
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                kDestination(bogus),
                Ter(tecNO_DST));

            // preclaim: tecDST_TAG_NEEDED
            Account const dest{"dest"};
            env.fund(XRP(1'000), dest);
            env(fset(dest, asfRequireDest));
            env.close();
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                kDestination(dest),
                Ter(tecDST_TAG_NEEDED));

            // preclaim: tecNO_PERMISSION
            env(fclear(dest, asfRequireDest));
            env(fset(dest, asfDepositAuth));
            env.close();
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                kDestination(dest),
                Ter(tecNO_PERMISSION));

            // preclaim: tecFROZEN
            env(trust(dest, asset(1'000)));
            env(fclear(dest, asfDepositAuth));
            env(fset(issuer, asfGlobalFreeze));
            env.close();
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                kDestination(dest),
                Ter(tecFROZEN));

            // preclaim:: tecFROZEN (deep frozen)
            env(fclear(issuer, asfGlobalFreeze));
            env(trust(issuer, asset(1'000), dest, tfSetFreeze | tfSetDeepFreeze));
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                kDestination(dest),
                Ter(tecFROZEN));

            // preclaim: tecPSEUDO_ACCOUNT
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                kDestination(vaultInfo.pseudoAccount),
                Ter(tecPSEUDO_ACCOUNT));
        }

        if (brokerTest == LoanBrokerTest::CoverClawback)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() {
                return env.json(
                    coverClawback(alice),
                    kLoanBrokerId(brokerKeylet.key),
                    kAmount(vaultInfo.asset(2)));
            });

            if (asset.holds<Issue>())
            {
                // preclaim: AllowTrustLineClawback is not set
                env(coverClawback(issuer),
                    kLoanBrokerId(brokerKeylet.key),
                    kAmount(vaultInfo.asset(2)),
                    Ter(tecNO_PERMISSION));

                // preclaim: NoFreeze is set
                env(fset(issuer, asfAllowTrustLineClawback | asfNoFreeze));
                env.close();
                env(coverClawback(issuer),
                    kLoanBrokerId(brokerKeylet.key),
                    kAmount(vaultInfo.asset(2)),
                    Ter(tecNO_PERMISSION));
            }
            else
            {
                // preclaim: MPTCanClawback is not set or MPTCanLock is not set
                env(coverClawback(issuer),
                    kLoanBrokerId(brokerKeylet.key),
                    kAmount(vaultInfo.asset(2)),
                    Ter(tecNO_PERMISSION));
            }
            env.close();
        }

        if (brokerTest == LoanBrokerTest::Delete)
        {
            Account const borrower{"borrower"};
            env.fund(XRP(1'000), borrower);
            env(loan::set(borrower, brokerKeylet.key, asset(50).value()),
                Sig(sfCounterpartySignature, alice),
                Fee(env.current()->fees().base * 2));

            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() { return del(alice, brokerKeylet.key); });

            // preclaim: tecHAS_OBLIGATIONS
            env(del(alice, brokerKeylet.key), Ter(tecHAS_OBLIGATIONS));

            // Repay and delete the loan
            auto const loanKeylet = keylet::loan(brokerKeylet.key, 1);
            env(loan::pay(borrower, loanKeylet.key, asset(50).value()));
            env(loan::del(alice, loanKeylet.key));

            env(trust(issuer, asset(0), alice, tfSetFreeze | tfSetDeepFreeze));
            // preclaim: tecFROZEN (deep frozen)
            env(del(alice, brokerKeylet.key), Ter(tecFROZEN));
            env(trust(issuer, asset(0), alice, tfClearFreeze | tfClearDeepFreeze));

            // successful delete the loan broker object
            env(del(alice, brokerKeylet.key), Ter(tesSUCCESS));
        }
        else
        {
            env(del(alice, brokerKeylet.key));
        }

        if (brokerTest == LoanBrokerTest::Set)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() {
                return env.json(set(alice, vaultInfo.vaultID), kLoanBrokerId(brokerKeylet.key));
            });
            // preflight: temINVALID (empty/zero vault id)
            testZeroVaultID([&]() {
                return env.json(set(alice, vaultInfo.vaultID), kLoanBrokerId(brokerKeylet.key));
            });

            if (asset.holds<Issue>())
            {
                env(fclear(issuer, asfDefaultRipple));
                env.close();
                // preclaim: DefaultRipple is not set
                env(set(alice, vaultInfo.vaultID), Ter(terNO_RIPPLE));

                env(fset(issuer, asfDefaultRipple));
                env.close();
            }

            auto const amt =
                env.balance(alice) - env.current()->fees().accountReserve(env.ownerCount(alice));
            env(pay(alice, issuer, amt));

            // preclaim:: tecINSUFFICIENT_RESERVE
            env(set(alice, vaultInfo.vaultID), Ter(tecINSUFFICIENT_RESERVE));
        }
    }

    void
    testInvalidLoanBrokerCoverClawback()
    {
        testcase("Invalid LoanBrokerCoverClawback");
        using namespace jtx;
        using namespace loanBroker;

        // preflight
        {
            Account const alice{"alice"};
            Account const issuer{"issuer"};
            auto const usd = alice["USD"];
            Env env(*this);
            env.fund(XRP(100'000), alice);
            env.close();

            auto jtx = env.jt(coverClawback(alice), kAmount(usd(100)));

            // holder == account
            env(jtx, Ter(temINVALID));

            // holder == beast::zero
            STAmount const bad(Issue{usd.currency, beast::kZero}, 100);
            jtx.jv[sfAmount] = bad.getJson();
            jtx.stx = env.ust(jtx);
            Serializer s;
            jtx.stx->add(s);
            auto const jrr = env.rpc("submit", strHex(s.slice()))[jss::result];
            // fails in doSubmit() on STTx construction
            BEAST_EXPECT(jrr[jss::error] == "invalidTransaction");
            BEAST_EXPECT(jrr[jss::error_exception] == "invalid native account");
        }

        // preclaim

        // Issue:
        // AllowTrustLineClawback is not set or NoFreeze is set
        testLoanBroker({}, LoanBrokerTest::CoverClawback);

        // MPTIssue:
        // MPTCanClawback is not set
        testLoanBroker(
            [&](Env& env, Account const& issuer, Account const& alice) -> MPT {
                MPTTester const mpt({.env = env, .issuer = issuer, .holders = {alice}});
                return mpt;
            },
            LoanBrokerTest::CoverClawback);
    }

    void
    testInvalidLoanBrokerCoverDeposit()
    {
        testcase("Invalid LoanBrokerCoverDeposit");
        using namespace jtx;

        // preclaim:
        // tecWRONG_ASSET, tecINSUFFICIENT_FUNDS, frozen asset
        testLoanBroker({}, LoanBrokerTest::CoverDeposit);
    }

    void
    testInvalidLoanBrokerCoverWithdraw()
    {
        testcase("Invalid LoanBrokerCoverWithdraw");
        using namespace jtx;

        /*
        preflight: illegal net
        isLegalNet() check is probably redundant. STAmount parsing
        should throw an exception on deserialize

        preclaim: tecWRONG_ASSET, tecNO_DST, tecDST_TAG_NEEDED,
            tecNO_PERMISSION, checkFrozen failure, checkDeepFrozenFailure,
            second+third tecINSUFFICIENT_FUNDS (can this happen)?
        doApply: tecPATH_DRY (can it happen, funds already checked?)
         */
        testLoanBroker({}, LoanBrokerTest::CoverWithdraw);
    }

    void
    testInvalidLoanBrokerDelete()
    {
        using namespace jtx;
        testcase("Invalid LoanBrokerDelete");
        /*
        preclaim: tecHAS_OBLIGATIONS
            doApply:
            accountSend failure, removeEmptyHolding failure,
            all tecHAS_OBLIGATIONS (can any of these happen?)
        */
        testLoanBroker({}, LoanBrokerTest::Delete);
    }

    void
    testInvalidLoanBrokerSet()
    {
        using namespace jtx;
        testcase("Invalid LoanBrokerSet");

        /*preclaim: canAddHolding failure (can it happen with MPT?
              can't create Vault if CanTransfer is not enabled.)
            doApply:
            first+second dirLink failure, createPseudoAccount failure,
            addEmptyHolding failure
            can any of these happen?
        */
        testLoanBroker({}, LoanBrokerTest::Set);
    }

    void
    testLoanBrokerCoverDepositNullVault()
    {
        // This test is lifted directly from
        // https://bugs.immunefi.com/dashboard/submission/57808
        using namespace jtx;
        Env env(*this);

        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        env.close();

        // Create a Vault owned by alice with an XRP asset
        PrettyAsset const asset{xrpIssue(), 1};
        Vault const vault{env};
        auto const [createTx, vaultKeylet] = vault.create({.owner = alice, .asset = asset});
        env(createTx);
        env.close();

        // Predict LoanBroker key using alice's current sequence BEFORE submit
        auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice));

        // Create LoanBroker pointing to the vault
        env(loanBroker::set(alice, vaultKeylet.key));
        env.close();

        // Build the CoverDeposit STTx directly
        STTx tx{ttLOAN_BROKER_COVER_DEPOSIT, [](STObject&) {}};
        tx.setAccountID(sfAccount, alice.id());
        tx.setFieldH256(sfLoanBrokerID, brokerKeylet.key);
        tx.setFieldAmount(sfAmount, asset(1));

        // Create a writable view cloned from the current ledger and remove the
        // vault SLE
        OpenView ov{*env.current()};
        test::StreamSink sink{beast::Severity::Warning};
        beast::Journal const jlog{sink};
        ApplyContext ac{env.app(), ov, tx, tesSUCCESS, env.current()->fees().base, TapNone, jlog};

        if (auto sleBroker = ac.view().peek(keylet::loanbroker(brokerKeylet.key)))
        {
            auto const vaultID = (*sleBroker)[sfVaultID];
            if (auto sleVault = ac.view().peek(keylet::vault(vaultID)))
            {
                ac.view().erase(sleVault);
            }
        }

        // Invoke preclaim against the mutated (ApplyView) view; triggers
        // nullptr deref
        PreclaimContext const pctx{env.app(), ac.view(), tesSUCCESS, tx, TapNone, jlog};
        (void)LoanBrokerCoverDeposit::preclaim(pctx);
    }

    void
    testRequireAuth()
    {
        testcase("Require Auth - Implicit Pseudo-account authorization");
        using namespace jtx;
        using namespace loanBroker;

        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Env env(*this);
        Vault vault{env};

        env.fund(XRP(100'000), issuer, alice);
        env.close();

        auto asset = MPTTester({
            .env = env,
            .issuer = issuer,
            .holders = {alice},
            .flags = kMptDexFlags | tfMPTRequireAuth | tfMPTCanClawback | tfMPTCanLock,
            .authHolder = true,
        });

        env(pay(issuer, alice, asset(100'000)));
        env.close();

        // Alice is not authorized, can still create the vault
        asset.authorize({.account = issuer, .holder = alice, .flags = tfMPTUnauthorize});
        auto [tx, vaultKeylet] = vault.create({.owner = alice, .asset = asset});
        env(tx);
        env.close();

        auto const le = env.le(vaultKeylet);
        VaultInfo vaultInfo = [&]() {
            if (BEAST_EXPECT(le))
                return VaultInfo{asset, vaultKeylet.key, le->at(sfAccount)};
            return VaultInfo{asset, {}, {}};
        }();
        if (vaultInfo.vaultID == uint256{})
            return;

        // Can't unauthorize Vault pseudo-account
        asset.authorize(
            {.account = issuer,
             .holder = vaultInfo.pseudoAccount,
             .flags = tfMPTUnauthorize,
             .err = tecNO_PERMISSION});

        auto forUnauthAuth = [&](auto&& doTx) {
            for (auto const flag : {tfMPTUnauthorize, 0u})
            {
                asset.authorize({.account = issuer, .holder = alice, .flags = flag});
                env.close();
                doTx(flag == 0);
                env.close();
            }
        };

        // Can't deposit into Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = asset(51)}),
                err);
        });

        // Can't withdraw from Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(vault.withdraw({.depositor = alice, .id = vaultKeylet.key, .amount = asset(1)}),
                err);
        });

        auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice));
        // Can create LoanBroker if the vault owner is not authorized
        forUnauthAuth([&](auto) { env(set(alice, vaultInfo.vaultID)); });

        auto const broker = env.le(brokerKeylet);
        if (!BEAST_EXPECT(broker))
            return;
        Account const brokerPseudo("pseudo", broker->at(sfAccount));

        // Can't unauthorize LoanBroker pseudo-account
        asset.authorize(
            {.account = issuer,
             .holder = brokerPseudo,
             .flags = tfMPTUnauthorize,
             .err = tecNO_PERMISSION});

        // Can't cover deposit into Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)), err);
        });

        // Can't cover withdraw from Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? Ter(tecNO_AUTH) : Ter(tesSUCCESS);
            env(coverWithdraw(alice, brokerKeylet.key, vaultInfo.asset(5)), err);
        });

        // Issuer can always cover clawback. The holder authorization is n/a.
        forUnauthAuth([&](bool) {
            env(coverClawback(issuer),
                kLoanBrokerId(brokerKeylet.key),
                kAmount(vaultInfo.asset(1)));
        });
    }

    void
    testLoanBrokerSetDebtMaximum()
    {
        testcase("testLoanBrokerSetDebtMaximum");
        using namespace jtx;
        using namespace loanBroker;
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Env env(*this);
        Vault const vault{env};

        env.fund(XRP(100'000), issuer, alice);
        env.close();

        PrettyAsset const asset = [&]() {
            MPTTester mptt{env, issuer, kMptInitNoFund};
            mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            env.close();
            PrettyAsset const mptAsset = mptt["MPT"];
            mptt.authorize({.account = alice});
            env.close();
            return mptAsset;
        }();

        env(pay(issuer, alice, asset(100'000)));
        env.close();

        auto [tx, vaultKeylet] = vault.create({.owner = alice, .asset = asset});
        env(tx);
        env.close();
        auto const le = env.le(vaultKeylet);
        VaultInfo const vaultInfo = [&]() {
            if (BEAST_EXPECT(le))
                return VaultInfo{asset, vaultKeylet.key, le->at(sfAccount)};
            return VaultInfo{asset, {}, {}};
        }();
        if (vaultInfo.vaultID == uint256{})
            return;

        env(vault.deposit({.depositor = alice, .id = vaultKeylet.key, .amount = asset(50)}));
        env.close();

        auto const brokerKeylet = keylet::loanbroker(alice.id(), env.seq(alice));
        env(set(alice, vaultInfo.vaultID));
        env.close();

        Account const borrower{"borrower"};
        env.fund(XRP(1'000), borrower);
        env(loan::set(borrower, brokerKeylet.key, asset(50).value()),
            Sig(sfCounterpartySignature, alice),
            Fee(env.current()->fees().base * 2));
        auto const broker = env.le(brokerKeylet);
        if (!BEAST_EXPECT(broker))
            return;

        BEAST_EXPECT(broker->at(sfDebtTotal) == 50);
        auto debtTotal = broker->at(sfDebtTotal);

        auto tx2 = set(alice, vaultInfo.vaultID);
        tx2[sfLoanBrokerID] = to_string(brokerKeylet.key);
        tx2[sfDebtMaximum] = debtTotal - 1;
        env(tx2, Ter(tecLIMIT_EXCEEDED));

        tx2[sfDebtMaximum] = debtTotal + 1;
        env(tx2, Ter(tesSUCCESS));

        tx2[sfDebtMaximum] = 0;
        env(tx2, Ter(tesSUCCESS));

        tx2[sfDebtMaximum] = json::Value::kMaxInt;
        env(tx2, Ter(tesSUCCESS));

        {
            auto const dm = power(2, 64) - 1;
            BEAST_EXPECT(dm > kMaxMpTokenAmount);
            tx2[sfDebtMaximum] = dm;
            env(tx2, Ter(temINVALID));
        }

        {
            auto const dm = power(2, 63) - 1;
            BEAST_EXPECTS(dm > kMaxMpTokenAmount, to_string(dm));
            tx2[sfDebtMaximum] = dm;
            env(tx2, Ter(temINVALID));
        }

        {
            auto const dm = power(2, 63) - 3;
            BEAST_EXPECTS(dm == kMaxMpTokenAmount, to_string(dm));
            tx2[sfDebtMaximum] = dm;
            env(tx2, Ter(tesSUCCESS));
        }

        {
            auto const dm = 2 * (power(2, 62) - 1) + 1;
            BEAST_EXPECTS(dm == kMaxMpTokenAmount, to_string(dm));
            tx2[sfDebtMaximum] = dm;
            env(tx2, Ter(tesSUCCESS));
        }

        tx2[sfDebtMaximum] = Number{9223372036854775807, 0};
        env(tx2, Ter(tesSUCCESS));
    }

    void
    testRIPD4323()
    {
        testcase << "RIPD-4323";
        using namespace jtx;
        Account const issuer("issuer");
        Account const holder("holder");
        Account const& broker = issuer;

        auto test = [&](auto&& getToken) {
            Env env(*this);

            env.fund(XRP(1'000), issuer, holder);
            env.close();

            auto const [token, deposit, err] = getToken(env);

            Vault const vault(env);
            auto const [tx, keylet] = vault.create({.owner = broker, .asset = token.asset()});
            env(tx);
            env.close();

            env(vault.deposit({.depositor = broker, .id = keylet.key, .amount = deposit}),
                Ter(err));
            env.close();

            auto const brokerKeylet = keylet::loanbroker(broker, env.seq(broker));

            env(loanBroker::set(broker, keylet.key));
            env.close();

            env(loanBroker::coverDeposit(broker, brokerKeylet.key, deposit), Ter(err));
            env.close();
        };

        test([&](Env&) {
            // issuer can issue any amount
            auto const token = issuer["IOU"];
            return std::make_tuple(token, token(1'000), tesSUCCESS);
        });
        std::vector<std::tuple<
            std::uint64_t,                 // pay to holder
            std::optional<std::uint64_t>,  // max amount
            std::uint64_t,                 // deposit amount
            TER>>                          // expected error
            const mptTests = {
                // issuer can issue up to 2'000 tokens
                {2'000, 4'000, 1'000, tesSUCCESS},
                // issuer can issue 500 tokens (250 VaultDeposit +
                // 250 LoanBrokerCoverDeposit)
                {2'000, 2'500, 250, tesSUCCESS},
                // issuer can issue 500 tokens (250 VaultDeposit +
                // 250 LoanBrokerCoverDeposit). MaximumAmount is default.
                {kMaxMpTokenAmount - 500, std::nullopt, 250, tesSUCCESS},
                // issuer can issue 500, and fails on depositing 1'000
                {2'000, 2'500, 1'000, tecINSUFFICIENT_FUNDS},
                // issuer has already issued MaximumAmount
                {2'000, 2'000, 1'000, tecINSUFFICIENT_FUNDS},
                // issuer has already issued MaximumAmount. MaximumAmount is
                // default.
                {kMaxMpTokenAmount, std::nullopt, 250, tecINSUFFICIENT_FUNDS},
            };
        for (auto const& [pay, max, deposit, err] : mptTests)
        {
            test([&](Env& env) -> std::tuple<MPT, PrettyAmount, TER> {
                MPT const token = MPTTester(
                    {.env = env,
                     .issuer = issuer,
                     .holders = {holder},
                     .pay = pay,
                     .flags = kMptDexFlags,
                     .maxAmt = max});
                return std::make_tuple(token, token(deposit), err);
            });
        }
    }

    void
    testAmB06VaultFreezeCheckMissing()
    {
        testcase << "RIPD-4466 - LoanBrokerSet disallows frozen vaults";
        using namespace jtx;
        Env env(*this);

        Account const issuer{"issuer"}, lender{"lender"}, borrower{"borrower"};
        env.fund(XRP(20'000), issuer, lender, borrower);
        auto const iou = issuer["IOU"];

        Vault const vault{env};
        auto [tx, vaultKeylet] = vault.create({.owner = lender, .asset = iou.asset()});
        env(tx);
        env.close();

        // Get vault pseudo-account and FREEZE it
        auto const vaultSle = env.le(vaultKeylet);
        auto const vaultPseudo = vaultSle->at(sfAccount);
        auto const vaultPseudoAcct = Account("VaultPseudo", vaultPseudo);
        env(trust(issuer, vaultPseudoAcct["IOU"](0), tfSetFreeze));

        env(loanBroker::set(lender, vaultKeylet.key), Ter(tecFROZEN));
    }

    void
    testRIPD4274IOU()
    {
        using namespace jtx;
        Account const issuer("broker");
        Account const broker("issuer");
        Account const dest("destination");
        auto const token = issuer["IOU"];

        enum class TrustState {
            RequireAuth,
            ZeroLimit,
            ReachedLimit,
            NearLimit,
            NoTrustLine,
        };

        auto test = [&](TrustState trustState) {
            Env env(*this);

            testcase << "RIPD-4274 IOU with state: " << static_cast<int>(trustState);

            auto setTrustLine = [&](Account const& acct, TrustState state) {
                switch (state)
                {
                    case TrustState::RequireAuth:
                        env(trust(issuer, token(0), acct, tfSetfAuth));
                        break;
                    case TrustState::ZeroLimit: {
                        auto jv = trust(acct, token(0));
                        // set QualityIn so that the trustline is not
                        // auto-deleted
                        jv[sfQualityIn] = 10'000'000;
                        env(jv);
                    }
                    break;
                    case TrustState::ReachedLimit: {
                        env(trust(acct, token(1'000)));
                        env(pay(issuer, acct, token(1'000)));
                        env.close();
                    }
                    break;
                    case TrustState::NearLimit: {
                        env(trust(acct, token(1'000)));
                        env(pay(issuer, acct, token(950)));
                        env.close();
                    }
                    break;
                    case TrustState::NoTrustLine:
                        // don't create a trustline
                        break;
                    default:
                        BEAST_EXPECT(false);
                }
                env.close();
            };

            env.fund(XRP(1'000), issuer, broker, dest);
            env.close();

            if (trustState == TrustState::RequireAuth)
            {
                env(fset(issuer, asfRequireAuth));
                env.close();

                setTrustLine(broker, TrustState::RequireAuth);
            }

            setTrustLine(dest, trustState);

            env(trust(broker, token(2'000), 0));
            env(pay(issuer, broker, token(2'000)));
            env.close();

            Vault const vault(env);
            auto const [tx, keylet] = vault.create({.owner = broker, .asset = token.asset()});
            env(tx);
            env.close();

            // Test Vault withdraw
            env(vault.deposit({.depositor = broker, .id = keylet.key, .amount = token(1'000)}));
            env.close();

            env(vault.withdraw({.depositor = broker, .id = keylet.key, .amount = token(1'000)}),
                loanBroker::kDestination(dest),
                Ter(std::ignore));
            BEAST_EXPECT(env.ter() == tecNO_LINE);
            env.close();

            env(vault.withdraw({.depositor = broker, .id = keylet.key, .amount = token(1'000)}));

            // Test LoanBroker withdraw
            auto const brokerKeylet = keylet::loanbroker(broker, env.seq(broker));

            env(loanBroker::set(broker, keylet.key));
            env.close();

            env(loanBroker::coverDeposit(broker, brokerKeylet.key, token(1'000)));
            env.close();

            env(loanBroker::coverWithdraw(broker, brokerKeylet.key, token(100)),
                loanBroker::kDestination(dest),
                Ter(std::ignore));
            BEAST_EXPECT(env.ter() == tecNO_LINE);
            env.close();

            // Clearing RequireAuth shouldn't change the result
            if (trustState == TrustState::RequireAuth)
            {
                env(fclear(issuer, asfRequireAuth));
                env.close();

                env(loanBroker::coverWithdraw(broker, brokerKeylet.key, token(100)),
                    loanBroker::kDestination(dest),
                    Ter(std::ignore));
                BEAST_EXPECT(env.ter() == tecNO_LINE);
                env.close();
            }
        };

        test(TrustState::RequireAuth);
        test(TrustState::ZeroLimit);
        test(TrustState::ReachedLimit);
        test(TrustState::NearLimit);
        test(TrustState::NoTrustLine);
    }

    void
    testRIPD4274MPT()
    {
        using namespace jtx;
        Account const issuer("broker");
        Account const broker("issuer");
        Account const dest("destination");

        enum class MPTState {
            RequireAuth,
            ReachedMAX,
            NoMPT,
        };

        auto test = [&](MPTState mptState) {
            Env env(*this);

            testcase << "RIPD-4274 MPT with state: " << static_cast<int>(mptState);

            env.fund(XRP(1'000), issuer, broker, dest);
            env.close();

            auto const maybeToken = [&]() -> std::optional<MPT> {
                switch (mptState)
                {
                    case MPTState::RequireAuth: {
                        auto tester = MPTTester(
                            {.env = env,
                             .issuer = issuer,
                             .holders = {broker, dest},
                             .pay = 2'000,
                             .flags = kMptDexFlags | tfMPTRequireAuth,
                             .authHolder = true,
                             .maxAmt = 5'000});
                        // unauthorize dest
                        tester.authorize(
                            {.account = issuer, .holder = dest, .flags = tfMPTUnauthorize});
                        return tester;
                    }
                    case MPTState::ReachedMAX: {
                        auto tester = MPTTester(
                            {.env = env,
                             .issuer = issuer,
                             .holders = {broker, dest},
                             .pay = 2'000,
                             .flags = kMptDexFlags,
                             .maxAmt = 4'000});
                        BEAST_EXPECT(env.balance(issuer, tester) == tester(-4'000));
                        return tester;
                    }
                    case MPTState::NoMPT: {
                        return MPTTester(
                            {.env = env,
                             .issuer = issuer,
                             .holders = {broker},
                             .pay = 2'000,
                             .flags = kMptDexFlags,
                             .maxAmt = 4'000});
                    }
                    default:
                        return std::nullopt;
                }
            }();
            if (!BEAST_EXPECT(maybeToken))
                return;

            auto const& token = *maybeToken;

            Vault const vault(env);
            auto const [tx, keylet] = vault.create({.owner = broker, .asset = token.asset()});
            env(tx);
            env.close();

            // Test Vault withdraw
            env(vault.deposit({.depositor = broker, .id = keylet.key, .amount = token(1'000)}));
            env.close();

            env(vault.withdraw({.depositor = broker, .id = keylet.key, .amount = token(1'000)}),
                loanBroker::kDestination(dest),
                Ter(std::ignore));

            // Shouldn't fail if at MaximumAmount since no new tokens are issued
            TER const err = mptState == MPTState::ReachedMAX ? TER(tesSUCCESS) : tecNO_AUTH;
            BEAST_EXPECT(env.ter() == err);
            env.close();

            if (!isTesSuccess(err))
            {
                env(vault.withdraw(
                    {.depositor = broker, .id = keylet.key, .amount = token(1'000)}));
            }

            // Test LoanBroker withdraw
            auto const brokerKeylet = keylet::loanbroker(broker, env.seq(broker));

            env(loanBroker::set(broker, keylet.key));
            env.close();

            env(loanBroker::coverDeposit(broker, brokerKeylet.key, token(1'000)));
            env.close();

            env(loanBroker::coverWithdraw(broker, brokerKeylet.key, token(100)),
                loanBroker::kDestination(dest),
                Ter(std::ignore));
            BEAST_EXPECT(env.ter() == err);
            env.close();
        };

        test(MPTState::RequireAuth);
        test(MPTState::ReachedMAX);
        test(MPTState::NoMPT);
    }

    void
    testRIPD4274()
    {
        testRIPD4274IOU();
        testRIPD4274MPT();
    }

public:
    void
    run() override
    {
        testLoanBrokerSetDebtMaximum();
        testLoanBrokerCoverDepositNullVault();

        testDisabled();
        testLifecycle();
        testInvalidLoanBrokerCoverClawback();
        testInvalidLoanBrokerCoverDeposit();
        testInvalidLoanBrokerCoverWithdraw();
        testInvalidLoanBrokerDelete();
        testInvalidLoanBrokerSet();
        testRequireAuth();

        testRIPD4323();
        testAmB06VaultFreezeCheckMissing();

        testRIPD4274();

        // TODO: Write clawback failure tests with an issuer / MPT that doesn't
        // have the right flags set.
    }
};

BEAST_DEFINE_TESTSUITE(LoanBroker, tx, xrpl);

}  // namespace xrpl::test
