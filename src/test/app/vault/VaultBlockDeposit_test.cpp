#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <string>

namespace xrpl {

class VaultBlockDeposit_test : public VaultTestBase
{
    void
    testVaultDepositBlockGeneral()
    {
        using namespace test::jtx;

        Env env{*this};
        Account const owner{"owner"};
        Account const other{"other"};

        env.fund(XRP(100'000'000), owner, other);
        Vault vault{env};
        PrettyAsset const asset = xrpIssue();
        std::string const prefix = "VaultDepositBlock: ";

        auto const blockVault = [&](TER expectedTer, Keylet const& keylet) {
            env(vault.set({.owner = owner, .id = keylet.key, .flags = tfVaultDepositBlock}),
                Ter(expectedTer));
        };

        auto const unblockVault = [&](TER expectedTer, Keylet const& keylet) {
            env(vault.set({.owner = owner, .id = keylet.key, .flags = tfVaultDepositUnblock}),
                Ter(expectedTer));
        };

        // Blocking Vault with the amendment disabled fails
        {
            testcase(prefix + "block/unblock fails when amendment is disabled");

            env.disableFeature(featureLendingProtocolV1_1);
            auto const [tx, keylet] = vault.create(
                {.owner = owner, .asset = asset, .flags = tfVaultOwnerCanBlockDeposit});
            env(tx, Ter(temINVALID_FLAG));
            env.close();

            blockVault(temINVALID_FLAG, keylet);
            unblockVault(temINVALID_FLAG, keylet);

            env.enableFeature(featureLendingProtocolV1_1);
        }

        // Block Vault deposits fails if the vault is not configured to allow blocking deposits
        {
            testcase(prefix + "block/unblock fails when vault is not configured");
            auto const [tx, keylet] = vault.create({.owner = owner, .asset = asset});
            env(tx);
            env.close();

            blockVault(tecNO_PERMISSION, keylet);
            unblockVault(tecNO_PERMISSION, keylet);

            env(vault.del({.owner = owner, .id = keylet.key}), Ter(tesSUCCESS));
            env.close();
        }

        auto const [tx, keylet] =
            vault.create({.owner = owner, .asset = asset, .flags = tfVaultOwnerCanBlockDeposit});
        env(tx);
        env.close();

        {
            testcase(prefix + "block/unblock succeeds");
            // deposit assets to show that blocking deposit does not block withdrawals
            env(vault.deposit({
                    .depositor = owner,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));
            env(vault.deposit({
                    .depositor = other,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));

            blockVault(tesSUCCESS, keylet);

            // Owner is blocked from depositing to the vault
            env(vault.deposit({
                    .depositor = owner,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tecNO_PERMISSION));

            // Other accounts are also blocked from depositing to the vault
            env(vault.deposit({
                    .depositor = other,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tecNO_PERMISSION));

            // Block vault withdrawal works as normal
            env(vault.withdraw({
                    .depositor = owner,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));

            env(vault.withdraw({
                    .depositor = other,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));

            unblockVault(tesSUCCESS, keylet);

            env(vault.deposit({
                    .depositor = owner,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));

            env(vault.deposit({
                    .depositor = other,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));

            // Withdraw to keep the vault empty
            env(vault.withdraw({
                    .depositor = owner,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));

            env(vault.withdraw({
                    .depositor = other,
                    .id = keylet.key,
                    .amount = XRP(10'000),
                }),
                Ter(tesSUCCESS));
        }

        {
            testcase(prefix + "block/unblock fails when caller is not owner");

            env(vault.set({.owner = other, .id = keylet.key, .flags = tfVaultDepositBlock}),
                Ter(tecNO_PERMISSION));

            blockVault(tesSUCCESS, keylet);

            env(vault.set({.owner = other, .id = keylet.key, .flags = tfVaultDepositUnblock}),
                Ter(tecNO_PERMISSION));

            unblockVault(tesSUCCESS, keylet);
        }

        {
            testcase(prefix + "unblock fails when vault is already unblocked");
            unblockVault(tecNO_PERMISSION, keylet);
        }

        {
            testcase(prefix + "block fails when vault is already blocked");
            blockVault(tesSUCCESS, keylet);
            blockVault(tecNO_PERMISSION, keylet);
            unblockVault(tesSUCCESS, keylet);
        }

        env(vault.del({.owner = owner, .id = keylet.key}));
    }

    void
    testPrivateVaultBlockDoesNotClearPrivate()
    {
        using namespace test::jtx;

        Env env{*this};
        Account const issuer{"issuer"};
        Account const owner{"owner"};
        env.fund(XRP(1000), issuer, owner);
        env.close();

        PrettyAsset const asset = issuer["IOU"];
        env.trust(asset(1000), owner);
        env(pay(issuer, owner, asset(500)));
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner,
             .asset = asset,
             .flags = tfVaultPrivate | tfVaultOwnerCanBlockDeposit});
        env(tx);
        env.close();

        {
            testcase("blocking a private vault does not change lsfVaultPrivate flag");
            auto setTx =
                vault.set({.owner = owner, .id = keylet.key, .flags = tfVaultDepositBlock});
            env(setTx, Ter(tesSUCCESS));
            auto const sleVault = env.le(keylet);
            if (!BEAST_EXPECT(sleVault))
                return;
            BEAST_EXPECT(sleVault->isFlag(lsfVaultDepositBlocked));
            BEAST_EXPECT(sleVault->isFlag(lsfVaultPrivate));
        }

        {
            testcase("unblocking a private vault does not change lsfVaultPrivate flag");
            auto setTx =
                vault.set({.owner = owner, .id = keylet.key, .flags = tfVaultDepositUnblock});
            env(setTx, Ter(tesSUCCESS));
            auto const sleVault = env.le(keylet);
            if (!BEAST_EXPECT(sleVault))
                return;
            BEAST_EXPECT(!sleVault->isFlag(lsfVaultDepositBlocked));
            BEAST_EXPECT(sleVault->isFlag(lsfVaultPrivate));
        }
    }

public:
    void
    run() override
    {
        testVaultDepositBlockGeneral();
        testPrivateVaultBlockDoesNotClearPrivate();
    }
};

BEAST_DEFINE_TESTSUITE(VaultBlockDeposit, app, xrpl);

}  // namespace xrpl
