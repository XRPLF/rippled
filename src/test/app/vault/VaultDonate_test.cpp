#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <string>
#include <utility>

namespace xrpl {

class VaultDonate_test : public VaultTestBase
{
    void
    testVaultDepositDonate()
    {
        using namespace test::jtx;
        std::string const prefix = "VaultDeposit donate";

        Env env{*this};
        Vault const vault{env};

        auto const vaultShareBalance = [&](Keylet const& vaultKeylet) {
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault != nullptr);

            auto const sleIssuance = env.le(keylet::mptokenIssuance(sleVault->at(sfShareMPTID)));
            BEAST_EXPECT(sleIssuance != nullptr);

            return sleIssuance->at(sfOutstandingAmount);
        };

        auto const vaultAssetBalance = [&](Keylet const& vaultKeylet) {
            auto const sleVault = env.le(vaultKeylet);
            BEAST_EXPECT(sleVault != nullptr);

            return std::make_pair(sleVault->at(sfAssetsAvailable), sleVault->at(sfAssetsTotal));
        };

        Account const owner{"owner"};
        Account const depositor{"depositor"};
        env.fund(XRP(1'000'000), owner, depositor);
        env.close();

        auto const depositAmount = XRP(10);

        auto const [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(tx, Ter(tesSUCCESS));
        env.close();

        // With featureLendingProtocolV1_1 disabled, donations fail
        {
            testcase(prefix + " fails with featureLendingProtocolV1_1 disabled");
            env.disableFeature(featureLendingProtocolV1_1);
            auto const donateTx = vault.deposit({
                .depositor = owner,
                .id = keylet.key,
                .amount = depositAmount,
                .flags = tfVaultDonate,
            });
            env(donateTx, Ter(temINVALID_FLAG));
            env.enableFeature(featureLendingProtocolV1_1);
            env.close();
        }

        // Donation is not allowed to an empty vault
        {
            testcase(prefix + " fails to an empty vault");
            auto const donateTx = vault.deposit({
                .depositor = owner,
                .id = keylet.key,
                .amount = depositAmount,
                .flags = tfVaultDonate,
            });
            env(donateTx, Ter(tecNO_PERMISSION));
            env.close();
        }

        // Further unit tests require assets in the Vault
        env(vault.deposit({
                .depositor = depositor,
                .id = keylet.key,
                .amount = depositAmount,
            }),
            Ter(tesSUCCESS));
        env.close();

        // Donation is not allowed by a non-owner
        {
            testcase(prefix + " fails by a non-owner");
            auto const donateTx = vault.deposit({
                .depositor = depositor,
                .id = keylet.key,
                .amount = depositAmount,
                .flags = tfVaultDonate,
            });
            env(donateTx, Ter(tecNO_PERMISSION));
            env.close();
        }

        // Donation cannot exceed assets maximum
        {
            testcase(prefix + " cannot exceed assets maximum");
            auto setTx = vault.set({
                .owner = owner,
                .id = keylet.key,
            });
            setTx[sfAssetsMaximum] = XRP(30).number();
            env(setTx, Ter(tesSUCCESS));

            auto donateTx = vault.deposit({
                .depositor = owner,
                .id = keylet.key,
                .amount = depositAmount + XRP(30),
                .flags = tfVaultDonate,
            });

            env(donateTx, Ter(tecLIMIT_EXCEEDED));
            env.close();
        }

        {
            testcase(prefix + " succeeds");
            auto const shareBalance = vaultShareBalance(keylet);
            auto const [assetsAvailable, assetsTotal] = vaultAssetBalance(keylet);

            auto donateTx = vault.deposit({
                .depositor = owner,
                .id = keylet.key,
                .amount = depositAmount,
                .flags = tfVaultDonate,
            });
            env(donateTx, Ter(tesSUCCESS));
            env.close();

            auto const shareBalanceAfterDeposit = vaultShareBalance(keylet);
            auto const [assetsAvailableAfterDeposit, assetsTotalAfterDeposit] =
                vaultAssetBalance(keylet);

            BEAST_EXPECT(shareBalance == shareBalanceAfterDeposit);
            BEAST_EXPECT(assetsAvailable + depositAmount.number() == assetsAvailableAfterDeposit);
            BEAST_EXPECT(assetsTotal + depositAmount.number() == assetsTotalAfterDeposit);

            auto const sleVault = env.le(keylet);
            if (!BEAST_EXPECT(sleVault))
                return;

            // The depositor can withdraw their assets and the donated amount
            Asset const shareAsset(sleVault->at(sfShareMPTID));
            donateTx = vault.withdraw(
                {.depositor = depositor, .id = keylet.key, .amount = shareAsset(shareBalance)});
            env(donateTx, Ter(tesSUCCESS));

            auto const shareBalanceAfterWithdraw = vaultShareBalance(keylet);
            auto const [assetsAvailableAfterWithdraw, assetsTotalAfterWithdraw] =
                vaultAssetBalance(keylet);
            BEAST_EXPECT(shareBalanceAfterWithdraw == 0);
            BEAST_EXPECT(assetsAvailableAfterWithdraw == 0);
            BEAST_EXPECT(assetsTotalAfterWithdraw == 0);
        }

        // Test donation with non-1:1 share ratio.
        // A prior donation skews the ratio so that 1 share > 1 asset.
        // The donated amount must land exactly, not rounded via shares.
        {
            testcase(prefix + " succeeds with non-1:1 share ratio");

            // Create a fresh vault
            auto const [createTx, vk] = vault.create({.owner = owner, .asset = xrpIssue()});
            env(createTx, Ter(tesSUCCESS));
            env.close();

            // Depositor puts in 10 XRP → gets 10 shares at 1:1
            env(vault.deposit({
                    .depositor = depositor,
                    .id = vk.key,
                    .amount = XRP(10),
                }),
                Ter(tesSUCCESS));
            env.close();

            // Owner donates 7 XRP → ratio becomes 17 assets / 10 shares
            env(vault.deposit({
                    .depositor = owner,
                    .id = vk.key,
                    .amount = XRP(7),
                    .flags = tfVaultDonate,
                }),
                Ter(tesSUCCESS));
            env.close();

            auto const sharesAfterFirstDonate = vaultShareBalance(vk);
            auto const [availAfterFirstDonate, totalAfterFirstDonate] = vaultAssetBalance(vk);

            // Shares unchanged (donation doesn't mint shares)
            BEAST_EXPECT(sharesAfterFirstDonate == 10'000'000);
            // Assets increased by exactly the donated amount
            BEAST_EXPECT(availAfterFirstDonate == 17'000'000);
            BEAST_EXPECT(totalAfterFirstDonate == 17'000'000);

            // Donate again at the skewed 17:10 ratio — 3 XRP
            env(vault.deposit({
                    .depositor = owner,
                    .id = vk.key,
                    .amount = XRP(3),
                    .flags = tfVaultDonate,
                }),
                Ter(tesSUCCESS));
            env.close();

            auto const sharesAfterSecondDonate = vaultShareBalance(vk);
            auto const [availAfterSecondDonate, totalAfterSecondDonate] = vaultAssetBalance(vk);

            // Shares still unchanged
            BEAST_EXPECT(sharesAfterSecondDonate == 10'000'000);
            // Assets increased by exactly 3 XRP (20 total)
            BEAST_EXPECT(availAfterSecondDonate == 20'000'000);
            BEAST_EXPECT(totalAfterSecondDonate == 20'000'000);

            // Depositor withdraws all shares — should get all 20 XRP
            auto const sleVault = env.le(vk);
            if (!BEAST_EXPECT(sleVault))
                return;
            Asset const shareAsset(sleVault->at(sfShareMPTID));
            env(vault.withdraw(
                    {.depositor = depositor,
                     .id = vk.key,
                     .amount = shareAsset(sharesAfterSecondDonate)}),
                Ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(vaultShareBalance(vk) == 0);
            BEAST_EXPECT(vaultAssetBalance(vk).first == 0);
            BEAST_EXPECT(vaultAssetBalance(vk).second == 0);
        }
    }

public:
    void
    run() override
    {
        testVaultDepositDonate();
    }
};

BEAST_DEFINE_TESTSUITE(VaultDonate, app, xrpl);

}  // namespace xrpl
