#include <test/jtx.h>

#include <xrpld/app/tx/detail/LoanBrokerCoverDeposit.h>

#include <xrpl/beast/unit_test/suite.h>

namespace ripple {
namespace test {

class LoanBroker_test : public beast::unit_test::suite
{
    // Ensure that all the features needed for Lending Protocol are included,
    // even if they are set to unsupported.
    FeatureBitset const all{
        jtx::testable_amendments() | featureMPTokensV1 |
        featureSingleAssetVault | featureLendingProtocol};

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
            Vault vault{env};
            auto const [tx, keylet] =
                vault.create({.owner = alice, .asset = asset});
            env(tx, ter(goodVault ? ter(tesSUCCESS) : ter(temDISABLED)));
            env.close();
            BEAST_EXPECT(static_cast<bool>(env.le(keylet)) == goodVault);

            using namespace loanBroker;
            // Can't create a loan broker regardless of whether the vault exists
            env(set(alice, keylet.key), ter(temDISABLED));
            auto const brokerKeylet =
                keylet::loanbroker(alice.id(), env.seq(alice));
            // Other LoanBroker transactions are disabled, too.
            // 1. LoanBrokerCoverDeposit
            env(coverDeposit(alice, brokerKeylet.key, asset(1000)),
                ter(temDISABLED));
            // 2. LoanBrokerCoverWithdraw
            env(coverWithdraw(alice, brokerKeylet.key, asset(1000)),
                ter(temDISABLED));
            // 3. LoanBrokerCoverClawback
            env(coverClawback(alice), ter(temDISABLED));
            env(coverClawback(alice),
                loanBrokerID(brokerKeylet.key),
                ter(temDISABLED));
            env(coverClawback(alice), amount(asset(0)), ter(temDISABLED));
            env(coverClawback(alice),
                loanBrokerID(brokerKeylet.key),
                amount(asset(1000)),
                ter(temDISABLED));
            // 4. LoanBrokerDelete
            env(del(alice, brokerKeylet.key), ter(temDISABLED));
        };
        failAll(all - featureMPTokensV1);
        failAll(all - featureSingleAssetVault - featureLendingProtocol);
        failAll(all - featureSingleAssetVault);
        failAll(all - featureLendingProtocol, true);
    }

    struct VaultInfo
    {
        jtx::PrettyAsset asset;
        uint256 vaultID;
        jtx::Account pseudoAccount;
        VaultInfo(
            jtx::PrettyAsset const& asset_,
            uint256 const& vaultID_,
            AccountID const& pseudo)
            : asset(asset_), vaultID(vaultID_), pseudoAccount("vault", pseudo)
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
            testcase << "Lifecycle: "
                     << (asset.native()                ? "XRP "
                             : asset.holds<Issue>()    ? "IOU "
                             : asset.holds<MPTIssue>() ? "MPT "
                                                       : "Unknown ")
                     << label;
        }

        using namespace jtx;
        using namespace loanBroker;

        // Bogus assets to use in test cases
        static PrettyAsset const badMptAsset = [&]() {
            MPTTester badMptt{env, evan, mptInitNoFund};
            badMptt.create(
                {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
            env.close();
            return badMptt["BAD"];
        }();
        static PrettyAsset const badIouAsset = evan["BAD"];
        static Account const nonExistent{"NonExistent"};
        static PrettyAsset const ghostIouAsset = nonExistent["GST"];
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
            Account const pseudoAccount{
                "Broker pseudo-account", broker->at(sfAccount)};

            auto const pseudoKeylet = keylet::account(pseudoAccount);
            if (auto const pseudo = env.le(pseudoKeylet); BEAST_EXPECT(pseudo))
            {
                // log << "Pseudo-account after create: "
                //     << to_string(pseudo->getJson()) << std::endl
                //     << std::endl;
                BEAST_EXPECT(
                    pseudo->at(sfFlags) ==
                    (lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth));
                BEAST_EXPECT(pseudo->at(sfSequence) == 0);
                BEAST_EXPECT(pseudo->at(sfBalance) == beast::zero);
                BEAST_EXPECT(
                    pseudo->at(sfOwnerCount) ==
                    (vault.asset.raw().native() ? 0 : 1));
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
                    auto const& accountData =
                        accountInfo[jss::result][jss::account_data];
                    if (BEAST_EXPECT(accountData.isObject()))
                    {
                        BEAST_EXPECT(accountData[jss::Account] == pseudoStr);
                        BEAST_EXPECT(
                            accountData[sfLoanBrokerID] ==
                            to_string(keylet.key));
                    }
                    auto const& pseudoInfo =
                        accountInfo[jss::result][jss::pseudo_account];
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
                        BEAST_EXPECT(
                            broker->at(sfCoverAvailable) == amount.number());
                        env.require(balance(pseudoAccount, amount));
                    }
                };

            // Test Cover funding before allowing alterations
            env(coverDeposit(alice, uint256(0), vault.asset(10)),
                ter(temINVALID));
            env(coverDeposit(evan, keylet.key, vault.asset(10)),
                ter(tecNO_PERMISSION));
            env(coverDeposit(evan, keylet.key, vault.asset(0)),
                ter(temBAD_AMOUNT));
            env(coverDeposit(evan, keylet.key, vault.asset(-10)),
                ter(temBAD_AMOUNT));
            env(coverDeposit(alice, vault.vaultID, vault.asset(10)),
                ter(tecNO_ENTRY));

            verifyCoverAmount(0);

            // Test cover clawback failure cases BEFORE depositing any cover
            // Need one of brokerID or amount
            env(coverClawback(alice), ter(temINVALID));
            env(coverClawback(alice),
                loanBrokerID(uint256(0)),
                ter(temINVALID));
            env(coverClawback(alice), amount(XRP(1000)), ter(temBAD_AMOUNT));
            env(coverClawback(alice),
                amount(vault.asset(-10)),
                ter(temBAD_AMOUNT));
            // Clawbacks with an MPT need to specify the broker ID
            env(coverClawback(alice), amount(badMptAsset(1)), ter(temINVALID));
            env(coverClawback(evan),
                loanBrokerID(vault.vaultID),
                ter(tecNO_ENTRY));
            // Only the issuer can clawback
            env(coverClawback(alice),
                loanBrokerID(keylet.key),
                ter(tecNO_PERMISSION));
            if (vault.asset.raw().native())
            {
                // Can not clawback XRP under any circumstances
                env(coverClawback(issuer),
                    loanBrokerID(keylet.key),
                    ter(tecNO_PERMISSION));
            }
            else
            {
                if (vault.asset.raw().holds<Issue>())
                {
                    // Clawbacks without a loanBrokerID need to specify an IOU
                    // with the broker's pseudo-account as the issuer
                    env(coverClawback(alice),
                        amount(ghostIouAsset(1)),
                        ter(tecNO_ENTRY));
                    env(coverClawback(alice),
                        amount(badIouAsset(1)),
                        ter(tecOBJECT_NOT_FOUND));
                    // Pseudo-account is not for a broker
                    env(coverClawback(alice),
                        amount(vaultPseudoIouAsset(1)),
                        ter(tecOBJECT_NOT_FOUND));
                    // If we specify a pseudo-account as the IOU amount, it
                    // needs to match the loan broker
                    env(coverClawback(issuer),
                        loanBrokerID(keylet.key),
                        amount(badBrokerPseudoIouAsset(10)),
                        ter(tecWRONG_ASSET));
                    PrettyAsset const brokerWrongCurrencyAsset =
                        pseudoAccount["WAT"];
                    env(coverClawback(issuer),
                        loanBrokerID(keylet.key),
                        amount(brokerWrongCurrencyAsset(10)),
                        ter(tecWRONG_ASSET));
                }
                else
                {
                    // Clawbacks with an MPT need to specify the broker ID, even
                    // if the asset is valid
                    BEAST_EXPECT(vault.asset.raw().holds<MPTIssue>());
                    env(coverClawback(alice),
                        amount(vault.asset(10)),
                        ter(temINVALID));
                }
                // Since no cover has been deposited, there's nothing to claw
                // back
                env(coverClawback(issuer),
                    loanBrokerID(keylet.key),
                    amount(vault.asset(10)),
                    ter(tecINSUFFICIENT_FUNDS));
            }
            env.close();

            // Fund the cover deposit
            env(coverDeposit(alice, keylet.key, vault.asset(10)));
            env.close();
            verifyCoverAmount(10);

            // Test withdrawal failure cases
            env(coverWithdraw(alice, uint256(0), vault.asset(10)),
                ter(temINVALID));
            env(coverWithdraw(evan, keylet.key, vault.asset(10)),
                ter(tecNO_PERMISSION));
            env(coverWithdraw(evan, keylet.key, vault.asset(0)),
                ter(temBAD_AMOUNT));
            env(coverWithdraw(evan, keylet.key, vault.asset(-10)),
                ter(temBAD_AMOUNT));
            env(coverWithdraw(alice, vault.vaultID, vault.asset(10)),
                ter(tecNO_ENTRY));
            env(coverWithdraw(alice, keylet.key, vault.asset(900)),
                ter(tecINSUFFICIENT_FUNDS));

            // Skip this test for XRP, because that can always be sent
            if (!vault.asset.raw().native())
            {
                TER const expected = vault.asset.raw().holds<MPTIssue>()
                    ? tecNO_AUTH
                    : tecNO_LINE;
                env(coverWithdraw(alice, keylet.key, vault.asset(1)),
                    destination(bystander),
                    ter(expected));
            }

            // Can not withdraw to the zero address
            env(coverWithdraw(alice, keylet.key, vault.asset(1)),
                destination(AccountID{}),
                ter(temMALFORMED));

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
            env(coverWithdraw(alice, keylet.key, vault.asset(1)),
                destination(evan));
            env.close();
            verifyCoverAmount(7);

            // Withdraw some more. Send it to Evan. Very generous, considering
            // how much trouble he's been.
            env(coverWithdraw(alice, keylet.key, vault.asset(1)),
                destination(evan),
                dtag(3));
            env.close();
            verifyCoverAmount(6);

            if (!vault.asset.raw().native())
            {
                // Issuer claws back some of the cover
                env(coverClawback(issuer),
                    loanBrokerID(keylet.key),
                    amount(vault.asset(2)));
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
                             loanBrokerID(keylet.key),
                             fee(none),
                             seq(none),
                             sig(none)),
                         env.json(
                             coverClawback(issuer),
                             loanBrokerID(keylet.key),
                             amount(vault.asset(0)),
                             fee(none),
                             seq(none),
                             sig(none)),
                         env.json(
                             coverClawback(issuer),
                             loanBrokerID(keylet.key),
                             amount(vault.asset(6)),
                             fee(none),
                             seq(none),
                             sig(none)),
                         // amount will be truncated to what's available
                         env.json(
                             coverClawback(issuer),
                             loanBrokerID(keylet.key),
                             amount(vault.asset(100)),
                             fee(none),
                             seq(none),
                             sig(none)),
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
            env(set(alice, vault.vaultID), loanBrokerID(keylet.key));
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
                loanBrokerID(broker->key()),
                debtMaximum(Number(0)),
                data(""));
            env.close();

            // Check the updated fields
            if (BEAST_EXPECT(broker = env.le(keylet)))
            {
                BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                BEAST_EXPECT(!broker->isFieldPresent(sfData));
            }

            /////////////////////////////////////
            // try to delete the wrong broker object
            env(del(alice, vault.vaultID), ter(tecNO_ENTRY));
            // evan tries to delete the broker
            env(del(evan, keylet.key), ter(tecNO_PERMISSION));

            // Get the "bad" broker out of the way
            env(del(alice, badKeylet.key));
            env.close();

            // Note alice's balance of the asset and the broker account's cover
            // funds
            auto const aliceBalance = env.balance(alice, vault.asset);
            auto const coverFunds = env.balance(pseudoAccount, vault.asset);
            BEAST_EXPECT(coverFunds.number() == broker->at(sfCoverAvailable));
            BEAST_EXPECT(coverFunds != beast::zero);
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
                (aliceBalance.value().native()
                     ? STAmount(env.current()->fees().base.value())
                     : vault.asset(0));
            env.require(balance(alice, expectedBalance));
            env.require(balance(pseudoAccount, vault.asset(none)));
        }
    }

    void
    testLifecycle()
    {
        testcase("Lifecycle");
        using namespace jtx;

        // Create 3 loan brokers: one for XRP, one for an IOU, and one for an
        // MPT. That'll require three corresponding SAVs.
        Env env(*this, all);

        Account issuer{"issuer"};
        // For simplicity, alice will be the sole actor for the vault & brokers.
        Account alice{"alice"};
        // Evan will attempt to be naughty
        Account evan{"evan"};
        // Bystander doesn't have anything to do with the SAV or Broker, or any
        // of the relevant tokens
        Account bystander{"bystander"};
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

        MPTTester mptt{env, issuer, mptInitNoFund};
        mptt.create(
            {.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
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

            env(vault.deposit(
                {.depositor = alice, .id = keylet.key, .amount = asset(50)}));
            env.close();
        }
        VaultInfo const badVault = [&]() -> VaultInfo {
            auto [tx, keylet] =
                vault.create({.owner = alice, .asset = iouAsset});
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
                std::string const pseudoStr =
                    to_string(vault.pseudoAccount.id());
                auto const accountInfo = env.rpc("account_info", pseudoStr);
                if (BEAST_EXPECT(accountInfo.isObject()))
                {
                    auto const& accountData =
                        accountInfo[jss::result][jss::account_data];
                    if (BEAST_EXPECT(accountData.isObject()))
                    {
                        BEAST_EXPECT(accountData[jss::Account] == pseudoStr);
                        BEAST_EXPECT(
                            accountData[sfVaultID] == to_string(vault.vaultID));
                    }
                    auto const& pseudoInfo =
                        accountInfo[jss::result][jss::pseudo_account];
                    if (BEAST_EXPECT(pseudoInfo.isObject()))
                    {
                        BEAST_EXPECT(pseudoInfo[jss::type] == "Vault");
                    }
                }
            }

            using namespace loanBroker;
            using namespace ripple::Lending;

            TenthBips32 const tenthBipsZero{0};

            auto badKeylet = keylet::vault(alice.id(), env.seq(alice));
            // Try some failure cases
            // not the vault owner
            env(set(evan, vault.vaultID), ter(tecNO_PERMISSION));
            // not a vault
            env(set(alice, badKeylet.key), ter(tecNO_ENTRY));
            // flags are checked first
            env(set(evan, vault.vaultID, ~tfUniversal), ter(temINVALID_FLAG));
            // field length validation
            // sfData: good length, bad account
            env(set(evan, vault.vaultID),
                data(std::string(maxDataPayloadLength, 'X')),
                ter(tecNO_PERMISSION));
            // sfData: too long
            env(set(evan, vault.vaultID),
                data(std::string(maxDataPayloadLength + 1, 'Y')),
                ter(temINVALID));
            // sfManagementFeeRate: good value, bad account
            env(set(evan, vault.vaultID),
                managementFeeRate(maxManagementFeeRate),
                ter(tecNO_PERMISSION));
            // sfManagementFeeRate: too big
            env(set(evan, vault.vaultID),
                managementFeeRate(maxManagementFeeRate + TenthBips16(10)),
                ter(temINVALID));
            // sfCoverRateMinimum and sfCoverRateLiquidation are linked
            // Cover: good value, bad account
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate),
                coverRateLiquidation(maxCoverRate),
                ter(tecNO_PERMISSION));
            // CoverMinimum: too big
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate + 1),
                coverRateLiquidation(maxCoverRate + 1),
                ter(temINVALID));
            // CoverLiquidation: too big
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate / 2),
                coverRateLiquidation(maxCoverRate + 1),
                ter(temINVALID));
            // Cover: zero min, non-zero liquidation - implicit and
            // explicit zero values.
            env(set(evan, vault.vaultID),
                coverRateLiquidation(maxCoverRate),
                ter(temINVALID));
            env(set(evan, vault.vaultID),
                coverRateMinimum(tenthBipsZero),
                coverRateLiquidation(maxCoverRate),
                ter(temINVALID));
            // Cover: non-zero min, zero liquidation - implicit and
            // explicit zero values.
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate),
                ter(temINVALID));
            env(set(evan, vault.vaultID),
                coverRateMinimum(maxCoverRate),
                coverRateLiquidation(tenthBipsZero),
                ter(temINVALID));
            // sfDebtMaximum: good value, bad account
            env(set(evan, vault.vaultID),
                debtMaximum(Number(0)),
                ter(tecNO_PERMISSION));
            // sfDebtMaximum: overflow
            env(set(evan, vault.vaultID),
                debtMaximum(Number(1, 100)),
                ter(temINVALID));
            // sfDebtMaximum: negative
            env(set(evan, vault.vaultID),
                debtMaximum(Number(-1)),
                ter(temINVALID));

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
                    BEAST_EXPECT(
                        !broker->isFieldPresent(sfCoverRateLiquidation));
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateMinimum) == 0);
                    BEAST_EXPECT(broker->at(sfCoverRateLiquidation) == 0);

                    BEAST_EXPECT(
                        env.ownerCount(alice) == aliceOriginalCount + 4);
                },
                [&](SLE::const_ref broker) {
                    // Modifications

                    // Update the fields
                    auto const nextKeylet =
                        keylet::loanbroker(alice.id(), env.seq(alice));

                    // fields that can't be changed
                    // LoanBrokerID
                    env(set(alice, vault.vaultID),
                        loanBrokerID(nextKeylet.key),
                        ter(tecNO_ENTRY));
                    // VaultID
                    env(set(alice, nextKeylet.key),
                        loanBrokerID(broker->key()),
                        ter(tecNO_PERMISSION));
                    // Owner
                    env(set(evan, vault.vaultID),
                        loanBrokerID(broker->key()),
                        ter(tecNO_PERMISSION));
                    // ManagementFeeRate
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        managementFeeRate(maxManagementFeeRate),
                        ter(temINVALID));
                    // CoverRateMinimum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        coverRateMinimum(maxManagementFeeRate),
                        ter(temINVALID));
                    // CoverRateLiquidation
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        coverRateLiquidation(maxManagementFeeRate),
                        ter(temINVALID));

                    // fields that can be changed
                    testData = "Test Data 1234";
                    // Bad data: too long
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        data(std::string(maxDataPayloadLength + 1, 'W')),
                        ter(temINVALID));

                    // Bad debt maximum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        debtMaximum(Number(-175, -1)),
                        ter(temINVALID));
                    // Data & Debt maximum
                    env(set(alice, vault.vaultID),
                        loanBrokerID(broker->key()),
                        data(testData),
                        debtMaximum(Number(175, -1)));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(checkVL(broker->at(sfData), testData));
                    BEAST_EXPECT(broker->at(sfDebtMaximum) == Number(175, -1));
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
                    // Finally, create another Loan Broker with none of the
                    // values at default
                    return env.jt(
                        jv,
                        data(testData),
                        managementFeeRate(TenthBips16(123)),
                        debtMaximum(Number(9)),
                        coverRateMinimum(TenthBips32(100)),
                        coverRateLiquidation(TenthBips32(200)));
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
                        loanBrokerID(broker->key()),
                        data(""),
                        debtMaximum(Number(0)));
                },
                [&](SLE::const_ref broker) {
                    // Check the updated fields
                    BEAST_EXPECT(!broker->isFieldPresent(sfData));
                    BEAST_EXPECT(!broker->isFieldPresent(sfDebtMaximum));
                });
        }

        BEAST_EXPECT(env.ownerCount(alice) == aliceOriginalCount);
    }

    enum LoanBrokerTest {
        CoverClawback,
        CoverDeposit,
        CoverWithdraw,
        Delete,
        Set
    };

    void
    testLoanBroker(
        std::function<jtx::PrettyAsset(
            jtx::Env&,
            jtx::Account const&,
            jtx::Account const&)> getAsset,
        LoanBrokerTest brokerTest)
    {
        using namespace jtx;
        using namespace loanBroker;
        Account const issuer{"issuer"};
        Account const alice{"alice"};
        Env env(*this);
        Vault vault{env};

        env.fund(XRP(100'000), issuer, alice);
        env.close();

        PrettyAsset const asset = [&]() {
            if (getAsset)
                return getAsset(env, issuer, alice);
            env(trust(alice, issuer["IOU"](1'000'000)), THISLINE);
            env.close();
            return PrettyAsset(issuer["IOU"]);
        }();

        env(pay(issuer, alice, asset(100'000)), THISLINE);
        env.close();

        auto [tx, vaultKeylet] = vault.create({.owner = alice, .asset = asset});
        env(tx, THISLINE);
        env.close();
        auto const le = env.le(vaultKeylet);
        VaultInfo vaultInfo = [&]() {
            if (BEAST_EXPECT(le))
                return VaultInfo{asset, vaultKeylet.key, le->at(sfAccount)};
            return VaultInfo{asset, {}, {}};
        }();
        if (vaultInfo.vaultID == uint256{})
            return;

        env(vault.deposit(
                {.depositor = alice,
                 .id = vaultKeylet.key,
                 .amount = asset(50)}),
            THISLINE);
        env.close();

        auto const brokerKeylet =
            keylet::loanbroker(alice.id(), env.seq(alice));
        env(set(alice, vaultInfo.vaultID), THISLINE);
        env.close();

        auto broker = env.le(brokerKeylet);
        if (!BEAST_EXPECT(broker))
            return;

        auto testZeroBrokerID = [&](auto&& getTxJv) {
            auto jv = getTxJv();
            // empty broker ID
            jv[sfLoanBrokerID] = "";
            env(jv, ter(temINVALID), THISLINE);
            // zero broker ID
            jv[sfLoanBrokerID] = to_string(uint256{});
            // needs a flag to distinguish the parsed STTx from the prior
            // test
            env(jv, txflags(tfFullyCanonicalSig), ter(temINVALID), THISLINE);
        };
        auto testZeroVaultID = [&](auto&& getTxJv) {
            auto jv = getTxJv();
            // empty broker ID
            jv[sfVaultID] = "";
            env(jv, ter(temINVALID), THISLINE);
            // zero broker ID
            jv[sfVaultID] = to_string(uint256{});
            // needs a flag to distinguish the parsed STTx from the prior
            // test
            env(jv, txflags(tfFullyCanonicalSig), ter(temINVALID), THISLINE);
        };

        if (brokerTest == CoverDeposit)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() {
                return coverDeposit(alice, brokerKeylet.key, asset(10));
            });

            // preclaim: tecWRONG_ASSET
            env(coverDeposit(alice, brokerKeylet.key, issuer["BAD"](10)),
                ter(tecWRONG_ASSET),
                THISLINE);

            // preclaim: tecINSUFFICIENT_FUNDS
            env(pay(alice, issuer, asset(100'000 - 50)), THISLINE);
            env.close();
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)),
                ter(tecINSUFFICIENT_FUNDS));

            // preclaim: tecFROZEN
            env(fset(issuer, asfGlobalFreeze), THISLINE);
            env.close();
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)),
                ter(tecFROZEN),
                THISLINE);
        }
        else
            // Fund the cover deposit
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)),
                THISLINE);
        env.close();

        if (brokerTest == CoverWithdraw)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() {
                return coverWithdraw(alice, brokerKeylet.key, asset(10));
            });

            // preclaim: tecWRONG_ASSSET
            env(coverWithdraw(alice, brokerKeylet.key, issuer["BAD"](10)),
                ter(tecWRONG_ASSET),
                THISLINE);

            // preclaim: tecNO_DST
            Account const bogus{"bogus"};
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                destination(bogus),
                ter(tecNO_DST),
                THISLINE);

            // preclaim: tecDST_TAG_NEEDED
            Account const dest{"dest"};
            env.fund(XRP(1'000), dest);
            env(fset(dest, asfRequireDest), THISLINE);
            env.close();
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                destination(dest),
                ter(tecDST_TAG_NEEDED),
                THISLINE);

            // preclaim: tecNO_PERMISSION
            env(fclear(dest, asfRequireDest), THISLINE);
            env(fset(dest, asfDepositAuth), THISLINE);
            env.close();
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                destination(dest),
                ter(tecNO_PERMISSION),
                THISLINE);

            // preclaim: tecFROZEN
            env(trust(dest, asset(1'000)), THISLINE);
            env(fclear(dest, asfDepositAuth), THISLINE);
            env(fset(issuer, asfGlobalFreeze), THISLINE);
            env.close();
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                destination(dest),
                ter(tecFROZEN),
                THISLINE);

            // preclaim:: tecFROZEN (deep frozen)
            env(fclear(issuer, asfGlobalFreeze), THISLINE);
            env(trust(
                    issuer, asset(1'000), dest, tfSetFreeze | tfSetDeepFreeze),
                THISLINE);
            env(coverWithdraw(alice, brokerKeylet.key, asset(10)),
                destination(dest),
                ter(tecFROZEN),
                THISLINE);
        }

        if (brokerTest == CoverClawback)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() {
                return env.json(
                    coverClawback(alice),
                    loanBrokerID(brokerKeylet.key),
                    amount(vaultInfo.asset(2)));
            });

            if (asset.holds<Issue>())
            {
                // preclaim: AllowTrustLineClaback is not set
                env(coverClawback(issuer),
                    loanBrokerID(brokerKeylet.key),
                    amount(vaultInfo.asset(2)),
                    ter(tecNO_PERMISSION),
                    THISLINE);

                // preclaim: NoFreeze is set
                env(fset(issuer, asfAllowTrustLineClawback | asfNoFreeze),
                    THISLINE);
                env.close();
                env(coverClawback(issuer),
                    loanBrokerID(brokerKeylet.key),
                    amount(vaultInfo.asset(2)),
                    ter(tecNO_PERMISSION),
                    THISLINE);
            }
            else
            {
                // preclaim: MPTCanClawback is not set or MPTCanLock is not set
                env(coverClawback(issuer),
                    loanBrokerID(brokerKeylet.key),
                    amount(vaultInfo.asset(2)),
                    ter(tecNO_PERMISSION),
                    THISLINE);
            }
            env.close();
        }

        if (brokerTest == Delete)
        {
            Account const borrower{"borrower"};
            env.fund(XRP(1'000), borrower);
            env(loan::set(borrower, brokerKeylet.key, asset(50).value()),
                sig(sfCounterpartySignature, alice),
                fee(env.current()->fees().base * 2),
                THISLINE);

            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() { return del(alice, brokerKeylet.key); });

            // preclaim: tecHAS_OBLIGATIONS
            env(del(alice, brokerKeylet.key),
                ter(tecHAS_OBLIGATIONS),
                THISLINE);

            // Repay and delete the loan
            auto const loanKeylet = keylet::loan(brokerKeylet.key, 1);
            env(loan::pay(borrower, loanKeylet.key, asset(50).value()),
                THISLINE);
            env(loan::del(alice, loanKeylet.key), THISLINE);

            env(trust(issuer, asset(0), alice, tfSetFreeze | tfSetDeepFreeze),
                THISLINE);
            // preclaim: tecFROZEN (deep frozen)
            env(del(alice, brokerKeylet.key), ter(tecFROZEN), THISLINE);
            env(trust(
                    issuer, asset(0), alice, tfClearFreeze | tfClearDeepFreeze),
                THISLINE);

            // successful delete the loan broker object
            env(del(alice, brokerKeylet.key), ter(tesSUCCESS), THISLINE);
        }
        else
            env(del(alice, brokerKeylet.key), THISLINE);

        if (brokerTest == Set)
        {
            // preflight: temINVALID (empty/zero broker id)
            testZeroBrokerID([&]() {
                return env.json(
                    set(alice, vaultInfo.vaultID),
                    loanBrokerID(brokerKeylet.key));
            });
            // preflight: temINVALID (empty/zero vault id)
            testZeroVaultID([&]() {
                return env.json(
                    set(alice, vaultInfo.vaultID),
                    loanBrokerID(brokerKeylet.key));
            });

            if (asset.holds<Issue>())
            {
                env(fclear(issuer, asfDefaultRipple), THISLINE);
                env.close();
                // preclaim: DefaultRipple is not set
                env(set(alice, vaultInfo.vaultID), ter(terNO_RIPPLE), THISLINE);

                env(fset(issuer, asfDefaultRipple), THISLINE);
                env.close();
            }

            auto const amt = env.balance(alice) -
                env.current()->fees().accountReserve(env.ownerCount(alice));
            env(pay(alice, issuer, amt), THISLINE);

            // preclaim:: tecINSUFFICIENT_RESERVE
            env(set(alice, vaultInfo.vaultID),
                ter(tecINSUFFICIENT_RESERVE),
                THISLINE);
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
            auto const USD = alice["USD"];
            Env env(*this);
            env.fund(XRP(100'000), alice);
            env.close();

            auto jtx = env.jt(coverClawback(alice), amount(USD(100)));

            // holder == account
            env(jtx, ter(temINVALID), THISLINE);

            // holder == beast::zero
            STAmount bad(Issue{USD.currency, beast::zero}, 100);
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
        testLoanBroker({}, CoverClawback);

        // MPTIssue:
        // MPTCanClawback is not set
        testLoanBroker(
            [&](Env& env, Account const& issuer, Account const& alice) -> MPT {
                MPTTester mpt(
                    {.env = env, .issuer = issuer, .holders = {alice}});
                return mpt;
            },
            CoverClawback);
    }

    void
    testInvalidLoanBrokerCoverDeposit()
    {
        testcase("Invalid LoanBrokerCoverDeposit");
        using namespace jtx;

        // preclaim:
        // tecWRONG_ASSET, tecINSUFFICIENT_FUNDS, frozen asset
        testLoanBroker({}, CoverDeposit);
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
        testLoanBroker({}, CoverWithdraw);
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
        testLoanBroker({}, Delete);
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
        testLoanBroker({}, Set);
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
        Vault vault{env};
        auto const [createTx, vaultKeylet] =
            vault.create({.owner = alice, .asset = asset});
        env(createTx);
        env.close();

        // Predict LoanBroker key using alice's current sequence BEFORE submit
        auto const brokerKeylet =
            keylet::loanbroker(alice.id(), env.seq(alice));

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
        test::StreamSink sink{beast::severities::kWarning};
        beast::Journal jlog{sink};
        ApplyContext ac{
            env.app(),
            ov,
            tx,
            tesSUCCESS,
            env.current()->fees().base,
            tapNONE,
            jlog};

        if (auto sleBroker =
                ac.view().peek(keylet::loanbroker(brokerKeylet.key)))
        {
            auto const vaultID = (*sleBroker)[sfVaultID];
            if (auto sleVault = ac.view().peek(keylet::vault(vaultID)))
            {
                ac.view().erase(sleVault);
            }
        }

        // Invoke preclaim against the mutated (ApplyView) view; triggers
        // nullptr deref
        PreclaimContext pctx{
            env.app(), ac.view(), tesSUCCESS, tx, tapNONE, jlog};
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
            .flags = MPTDEXFlags | tfMPTRequireAuth | tfMPTCanClawback |
                tfMPTCanLock,
            .authHolder = true,
        });

        env(pay(issuer, alice, asset(100'000)));
        env.close();

        // Alice is not authorized, can still create the vault
        asset.authorize(
            {.account = issuer, .holder = alice, .flags = tfMPTUnauthorize});
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
                asset.authorize(
                    {.account = issuer, .holder = alice, .flags = flag});
                env.close();
                doTx(flag == 0);
                env.close();
            }
        };

        // Can't deposit into Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? ter(tecNO_AUTH) : ter(tesSUCCESS);
            env(vault.deposit(
                    {.depositor = alice,
                     .id = vaultKeylet.key,
                     .amount = asset(51)}),
                err);
        });

        // Can't withdraw from Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? ter(tecNO_AUTH) : ter(tesSUCCESS);
            env(vault.withdraw(
                    {.depositor = alice,
                     .id = vaultKeylet.key,
                     .amount = asset(1)}),
                err);
        });

        auto const brokerKeylet =
            keylet::loanbroker(alice.id(), env.seq(alice));
        // Can create LoanBroker if the vault owner is not authorized
        forUnauthAuth([&](auto) { env(set(alice, vaultInfo.vaultID)); });

        auto const broker = env.le(brokerKeylet);
        if (!BEAST_EXPECT(broker))
            return;
        Account brokerPseudo("pseudo", broker->at(sfAccount));

        // Can't unauthorize LoanBroker pseudo-account
        asset.authorize(
            {.account = issuer,
             .holder = brokerPseudo,
             .flags = tfMPTUnauthorize,
             .err = tecNO_PERMISSION});

        // Can't cover deposit into Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? ter(tecNO_AUTH) : ter(tesSUCCESS);
            env(coverDeposit(alice, brokerKeylet.key, vaultInfo.asset(10)),
                err);
        });

        // Can't cover withdraw from Vault if the vault owner is not authorized
        forUnauthAuth([&](bool authorized) {
            auto const err = !authorized ? ter(tecNO_AUTH) : ter(tesSUCCESS);
            env(coverWithdraw(alice, brokerKeylet.key, vaultInfo.asset(5)),
                err);
        });

        // Issuer can always cover clawback. The holder authorization is n/a.
        forUnauthAuth([&](bool) {
            env(coverClawback(issuer),
                loanBrokerID(brokerKeylet.key),
                amount(vaultInfo.asset(1)));
        });
    }

public:
    void
    run() override
    {
        testLoanBrokerCoverDepositNullVault();

        testDisabled();
        testLifecycle();
        testInvalidLoanBrokerCoverClawback();
        testInvalidLoanBrokerCoverDeposit();
        testInvalidLoanBrokerCoverWithdraw();
        testInvalidLoanBrokerDelete();
        testInvalidLoanBrokerSet();
        testRequireAuth();

        // TODO: Write clawback failure tests with an issuer / MPT that doesn't
        // have the right flags set.
    }
};

BEAST_DEFINE_TESTSUITE(LoanBroker, tx, ripple);

}  // namespace test
}  // namespace ripple
