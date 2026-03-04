#include <test/app/BaseInvariants_test.h>
//
#include <test/jtx.h>
#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/apply.h>

#include <string>

namespace xrpl {
namespace test {

/*
 * This class tests the invariants of the Single Asset Vault.
 * It uses the following conventions:
 * Account named A1 is the owner of the vault.
 * Account named A2 is a depositor.
 * Account named issuer is the issuer of the vault's asset.
 */
class VaultInvariants_test : public BaseInvariants_test
{
    // the Account that will be the asset issuer
    test::jtx::Account const issuer{"issuer"};

    std::optional<Keylet> vaultKey_;

    struct AccountAmount
    {
        AccountID account;
        int amount;
    };

    struct Adjustments
    {
        std::optional<int> assetsTotal = {};
        std::optional<int> assetsAvailable = {};
        std::optional<int> lossUnrealized = {};
        std::optional<int> assetsMaximum = {};
        std::optional<int> sharesTotal = {};
        std::optional<int> vaultAssets = {};
        std::optional<AccountAmount> accountAssets = {};
        std::optional<AccountAmount> accountShares = {};
    };

    Adjustments
    createVaultAdjustments(AccountID id, int adjustment, std::function<void(Adjustments&)> fn)
    {
        Adjustments sample = {
            .assetsTotal = adjustment,
            .assetsAvailable = adjustment,
            .lossUnrealized = std::nullopt,
            .assetsMaximum = std::nullopt,
            .sharesTotal = adjustment,
            .vaultAssets = adjustment,
            .accountAssets = AccountAmount{id, -adjustment},
            .accountShares = AccountAmount{id, adjustment},
        };

        fn(sample);
        return sample;
    }

    bool
    transferAssets(
        ApplyContext& ac,
        std::optional<AccountID const> sender,
        AccountID const& receiver,
        STAmount const& amount)
    {
        if (amount.native())
        {
            auto const keylet = keylet::account(receiver);
            auto sleAccount = ac.view().peek(keylet);
            if (!BEAST_EXPECT(sleAccount))
                return false;

            (*sleAccount)[sfBalance] = *(*sleAccount)[sfBalance] + amount;
            ac.view().update(sleAccount);
            return true;
        }

        if (!BEAST_EXPECT(sender))
            return false;

        return accountSend(
                   ac.view(), *sender, receiver, amount, ac.journal, WaiveTransferFee::Yes) ==
            tesSUCCESS;
    }

    bool
    adjustVaultAndDepositor(ApplyContext& ac, xrpl::Keylet keylet, Adjustments args)
    {
        auto vaultSle = ac.view().peek(keylet);
        if (!BEAST_EXPECT(vaultSle))
            return false;

        auto const mptIssuanceID = (*vaultSle)[sfShareMPTID];
        auto sleShares = ac.view().peek(keylet::mptIssuance(mptIssuanceID));
        if (!BEAST_EXPECT(sleShares))
            return false;

        // These two fields are adjusted in absolute terms
        if (args.lossUnrealized)
            (*vaultSle)[sfLossUnrealized] = *args.lossUnrealized;
        if (args.assetsMaximum)
            (*vaultSle)[sfAssetsMaximum] = *args.assetsMaximum;

        // Remaining fields are adjusted in terms of difference
        if (args.assetsTotal)
            (*vaultSle)[sfAssetsTotal] = *(*vaultSle)[sfAssetsTotal] + *args.assetsTotal;
        if (args.assetsAvailable)
            (*vaultSle)[sfAssetsAvailable] =
                *(*vaultSle)[sfAssetsAvailable] + *args.assetsAvailable;
        ac.view().update(vaultSle);

        if (args.sharesTotal)
        {
            (*sleShares)[sfOutstandingAmount] =
                *(*sleShares)[sfOutstandingAmount] + *args.sharesTotal;
            ac.view().update(sleShares);
        }

        auto const assets = *(*vaultSle)[sfAsset];
        auto const pseudoId = *(*vaultSle)[sfAccount];

        if (args.vaultAssets)
        {
            std::optional<AccountID> sender = std::nullopt;
            AccountID receiver = pseudoId;
            STAmount amount = STAmount{assets, *args.vaultAssets};

            if (!assets.native())
            {
                sender = *args.vaultAssets > 0 ? assets.getIssuer() : pseudoId;
                receiver = *args.vaultAssets > 0 ? pseudoId : assets.getIssuer();
                amount = STAmount{assets, std::abs(*args.vaultAssets)};
            }

            if (!BEAST_EXPECT(transferAssets(ac, sender, receiver, amount)))
                return false;
        }

        if (args.accountAssets)
        {
            auto const& pair = *args.accountAssets;
            std::optional<AccountID> sender = std::nullopt;
            AccountID receiver = pair.account;
            STAmount amount =
                assets.native() ? XRPAmount(pair.amount) : STAmount{assets, std::abs(pair.amount)};

            if (!assets.native())
            {
                sender = pair.amount > 0 ? assets.getIssuer() : pair.account;
                receiver = pair.amount > 0 ? pair.account : assets.getIssuer();
            }

            if (!BEAST_EXPECT(transferAssets(ac, sender, receiver, amount)))
                return false;
        }

        if (args.accountShares)
        {
            auto const& pair = *args.accountShares;
            auto sleMPToken = ac.view().peek(keylet::mptoken(mptIssuanceID, pair.account));
            if (!BEAST_EXPECT(sleMPToken))
                return false;
            (*sleMPToken)[sfMPTAmount] = *(*sleMPToken)[sfMPTAmount] + pair.amount;
            ac.view().update(sleMPToken);
        }

        return true;
    }

    /** Returns the keylet of the vault created by the most recent createVault()
     *  call. Must only be called from a Precheck lambda after createVault() has
     *  run as the Preclose.
     */
    std::optional<Keylet>
    vaultKeylet()
    {
        return vaultKey_;
    }

    Preclose
    createEmptyVault()
    {
        using namespace test::jtx;

        return [](Account const& acc, Account const&, Env& env) {
            Vault vault{env};
            auto [tx, _] = vault.create({.owner = acc, .asset = xrpIssue()});
            env(tx, ter(tesSUCCESS));
            return true;
        };
    }

    Preclose
    createVault(AssetType assetType)
    {
        using namespace test::jtx;
        return [&, assetType](Account const& A1, Account const& A2, Env& env) -> bool {
            env.fund(XRP(1000), issuer);
            env.close();
            PrettyAsset const asset = [&]() {
                switch (assetType)
                {
                    case AssetType::IOU: {
                        PrettyAsset const iouAsset = issuer["IOU"];
                        env(trust(A1, iouAsset(100'000'000)));
                        env(trust(A2, iouAsset(100'000'000)));
                        env(pay(issuer, A1, iouAsset(100'000'000)));
                        env(pay(issuer, A2, iouAsset(100'000'000)));
                        env.close();
                        return iouAsset;
                    }

                    case AssetType::MPT: {
                        MPTTester mptt{env, issuer, mptInitNoFund};
                        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                        PrettyAsset const mptAsset = mptt.issuanceID();
                        mptt.authorize({.account = A1});
                        mptt.authorize({.account = A2});
                        env(pay(issuer, A1, mptAsset(100'000'000)));
                        env(pay(issuer, A2, mptAsset(100'000'000)));
                        env.close();
                        return mptAsset;
                    }

                    case AssetType::XRP:
                    default:
                        return PrettyAsset{xrpIssue()};
                }
            }();

            Vault vault{env};
            auto const [tx, keylet] = vault.create({.owner = A1, .asset = asset});
            env(tx, ter(tesSUCCESS));
            env.close();

            vaultKey_ = keylet;
            BEAST_EXPECT(env.le(keylet));

            auto const depositVaultFunds = [&](Account const& acc, STAmount const& amt) {
                env(vault.deposit({
                        .depositor = acc,
                        .id = keylet.key,
                        .amount = amt,
                    }),
                    ter(tesSUCCESS));
                env.close();
            };

            auto const amount = assetType == AssetType::IOU ? asset(Number{1'234'567, -4})
                                                            : asset(Number{1'234'567});

            depositVaultFunds(A1, amount);
            depositVaultFunds(A2, amount);

            return true;
        };
    }

    void
    testVaultCreate()
    {
        using namespace test::jtx;

        std::string const prefix = "VaultCreate: ";

        testcase(prefix + "vault operation updated more than single vault");
        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const insertVault = [&](AccountID const& accId) {
                    auto const sequence = ac.view().seq();
                    auto const vaultKeylet = keylet::vault(accId, sequence);
                    auto vaultSle = std::make_shared<SLE>(vaultKeylet);
                    auto const vaultPage = ac.view().dirInsert(
                        keylet::ownerDir(accId), vaultSle->key(), describeOwnerDir(accId));
                    vaultSle->setFieldU64(sfOwnerNode, *vaultPage);
                    ac.view().insert(vaultSle);
                };
                insertVault(A1.id());
                insertVault(A2.id());
                return true;
            },
            XRPAmount{},
            STTx{
                ttVAULT_CREATE,
                [](STObject&) {},
            },
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase(prefix + "vault operation succeeded without modifying a vault");
        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "updated zero sized vault must have no assets outstanding");
        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                return adjustVaultAndDepositor(
                    ac, keylet, createVaultAdjustments(A1.id(), 0, [&](Adjustments& sample) {
                        sample.assetsTotal = 9;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "updated zero sized vault must have no assets available");
        doInvariantCheck(
            {
                "created vault must be empty",
                "updated zero sized vault must have no assets available",
                "assets available must not be greater than assets outstanding",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                return adjustVaultAndDepositor(
                    ac, keylet, createVaultAdjustments(A1.id(), 0, [&](Adjustments& sample) {
                        sample.assetsAvailable = 9;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "vault transaction must not change loss unrealized");
        doInvariantCheck(
            {
                "created vault must be empty",
                "loss unrealized must not exceed the difference between assets "
                "outstanding and available",
                "vault transaction must not change loss unrealized",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                return adjustVaultAndDepositor(
                    ac, keylet, createVaultAdjustments(A1.id(), 0, [&](Adjustments& sample) {
                        sample.lossUnrealized = 9;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "create operation must not have updated a vault");
        doInvariantCheck(
            {
                "created vault must be empty",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                auto vaultSle = ac.view().peek(keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;

                ac.view().update(vaultSle);
                (*sleShares)[sfOutstandingAmount] = 9;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "assets maximum must be positive");
        doInvariantCheck(
            {
                "assets maximum must be positive",
                "create operation must not have updated a vault",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                auto vaultSle = ac.view().peek(keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                (*vaultSle)[sfAssetsMaximum] = Number(-1);
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "valid vault pseudo-account");
        doInvariantCheck(
            {"create operation must not have updated a vault",
             "shares issuer and vault pseudo-account must be the same",
             "shares issuer must be a pseudo-account",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = keylet::vault(A1.id(), ac.view().seq());

                auto vaultSle = ac.view().peek(keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;

                ac.view().update(vaultSle);
                (*sleShares)[sfIssuer] = A1.id();
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createEmptyVault());

        testcase(prefix + "account root created illegally");
        doInvariantCheck(
            {"vault created by a wrong transaction type", "account root created illegally"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                // The code below will create a valid vault with (almost) all
                // the invariants holding. Except one: it is created by the
                // wrong transaction type.
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto vaultSle = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()), vaultSle->key(), describeOwnerDir(A1.id()));
                vaultSle->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =
                    ac.view().rules().enabled(featureSingleAssetVault) ? 0 : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);
                sleAccount->setFieldH256(sfVaultID, vaultKeylet.key);
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                vaultSle->at(sfAccount) = pseudoId;
                vaultSle->at(sfFlags) = 0;
                vaultSle->at(sfSequence) = sequence;
                vaultSle->at(sfOwner) = A1.id();
                vaultSle->at(sfAssetsTotal) = Number(0);
                vaultSle->at(sfAssetsAvailable) = Number(0);
                vaultSle->at(sfLossUnrealized) = Number(0);
                vaultSle->at(sfShareMPTID) = sharesMptId;
                vaultSle->at(sfWithdrawalPolicy) = vaultStrategyFirstComeFirstServe;

                ac.view().insert(vaultSle);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        testcase(prefix + "pseudo-account connected to vault");
        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same",
             "shares issuer pseudo-account must point back to the vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto vaultSle = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()), vaultSle->key(), describeOwnerDir(A1.id()));
                vaultSle->setFieldU64(sfOwnerNode, *vaultPage);

                auto pseudoId = pseudoAccountAddress(ac.view(), vaultKeylet.key);
                // Create pseudo-account.
                auto sleAccount = std::make_shared<SLE>(keylet::account(pseudoId));
                sleAccount->setAccountID(sfAccount, pseudoId);
                sleAccount->setFieldAmount(sfBalance, STAmount{});
                std::uint32_t const seqno =
                    ac.view().rules().enabled(featureSingleAssetVault) ? 0 : sequence;
                sleAccount->setFieldU32(sfSequence, seqno);
                sleAccount->setFieldU32(
                    sfFlags, lsfDisableMaster | lsfDefaultRipple | lsfDepositAuth);

                sleAccount->setFieldH256(sfVaultID, uint256(42));
                ac.view().insert(sleAccount);

                auto const sharesMptId = makeMptID(sequence, pseudoId);
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(pseudoId), sharesKeylet, describeOwnerDir(pseudoId));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                sleShares->at(sfIssuer) = pseudoId;
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                // vaultSle->at(sfAccount) = pseudoId;
                // Setting wrong pseudo account ID
                vaultSle->at(sfAccount) = A2.id();
                vaultSle->at(sfFlags) = 0;
                vaultSle->at(sfSequence) = sequence;
                vaultSle->at(sfOwner) = A1.id();
                vaultSle->at(sfAssetsTotal) = Number(0);
                vaultSle->at(sfAssetsAvailable) = Number(0);
                vaultSle->at(sfLossUnrealized) = Number(0);
                vaultSle->at(sfShareMPTID) = sharesMptId;
                vaultSle->at(sfWithdrawalPolicy) = vaultStrategyFirstComeFirstServe;

                ac.view().insert(vaultSle);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});

        testcase(prefix + "share issuer must exist");
        doInvariantCheck(
            {"shares issuer and vault pseudo-account must be the same", "shares issuer must exist"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto vaultSle = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()), vaultSle->key(), describeOwnerDir(A1.id()));
                vaultSle->setFieldU64(sfOwnerNode, *vaultPage);

                auto const sharesMptId = makeMptID(sequence, A2.id());
                auto const sharesKeylet = keylet::mptIssuance(sharesMptId);
                auto sleShares = std::make_shared<SLE>(sharesKeylet);
                auto const sharesPage = ac.view().dirInsert(
                    keylet::ownerDir(A2.id()), sharesKeylet, describeOwnerDir(A2.id()));
                sleShares->setFieldU64(sfOwnerNode, *sharesPage);

                sleShares->at(sfFlags) = 0;
                // Setting wrong pseudo account ID
                sleShares->at(sfIssuer) = AccountID(uint160(42));
                sleShares->at(sfOutstandingAmount) = 0;
                sleShares->at(sfSequence) = sequence;

                vaultSle->at(sfAccount) = A2.id();
                vaultSle->at(sfFlags) = 0;
                vaultSle->at(sfSequence) = sequence;
                vaultSle->at(sfOwner) = A1.id();
                vaultSle->at(sfAssetsTotal) = Number(0);
                vaultSle->at(sfAssetsAvailable) = Number(0);
                vaultSle->at(sfLossUnrealized) = Number(0);
                vaultSle->at(sfShareMPTID) = sharesMptId;
                vaultSle->at(sfWithdrawalPolicy) = vaultStrategyFirstComeFirstServe;

                ac.view().insert(vaultSle);
                ac.view().insert(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_CREATE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED});
    }

    void
    testVaultSet()
    {
        using namespace test::jtx;

        std::string const prefix = "VaultSet: ";

        testcase(prefix + "must not invalidate the vault");
        doInvariantCheck(
            {"set must not change assets outstanding",
             "set must not change assets available",
             "set must not change shares outstanding",
             "set must not change vault balance",
             "assets available must be positive",
             "assets available must not be greater than assets outstanding",
             "assets outstanding must be positive"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 0, [&](Adjustments& sample) {
                        sample.assetsAvailable = (DROPS_PER_XRP * -100).value();
                        sample.assetsTotal = (DROPS_PER_XRP * -200).value();
                        sample.sharesTotal = -1;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);

        testcase(prefix + "vault updated by a wrong transaction type");
        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                ac.view().erase(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "vault updated by a wrong transaction type 2");
        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const sequence = ac.view().seq();
                auto const vaultKeylet = keylet::vault(A1.id(), sequence);
                auto vaultSle = std::make_shared<SLE>(vaultKeylet);
                auto const vaultPage = ac.view().dirInsert(
                    keylet::ownerDir(A1.id()), vaultSle->key(), describeOwnerDir(A1.id()));
                vaultSle->setFieldU64(sfOwnerNode, *vaultPage);
                ac.view().insert(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        testcase(prefix + "vault updated by a wrong transaction type 3");
        doInvariantCheck(
            {"vault updated by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttPAYMENT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "updated vault must have shares");
        doInvariantCheck(
            {"updated vault must have shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                (*vaultSle)[sfAssetsMaximum] = 200;
                ac.view().update(vaultSle);

                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "violation of vault immutable data");
        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                vaultSle->setFieldIssue(sfAsset, STIssue{sfAsset, MPTIssue(MPTID(42))});
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "violation of vault immutable data 2");
        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                vaultSle->setAccountID(sfAccount, A2.id());
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "violation of vault immutable data 3");
        doInvariantCheck(
            {"violation of vault immutable data"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                (*vaultSle)[sfShareMPTID] = MPTID(42);
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "must not change vault balances");
        doInvariantCheck(
            {"vault transaction must not change loss unrealized",
             "set must not change assets outstanding"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 0, [&](Adjustments& sample) {
                        sample.lossUnrealized = 13;
                        sample.assetsTotal = 20;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);

        testcase(prefix + "must modify the vault");
        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;
                // Note, such an "orphaned" update of MPT issuance attached to a
                // vault is invalid; ttVAULT_SET must also update Vault object.
                sleShares->setFieldH256(sfDomainID, uint256(13));
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);

        testcase(prefix + "must not invalidate assets maximum");
        doInvariantCheck(
            {"set assets outstanding must not exceed assets maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 0, [&](Adjustments& sample) {
                        sample.assetsMaximum = 1;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);

        testcase(prefix + "assets maximum must be positive");
        doInvariantCheck(
            {"assets maximum must be positive"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 0, [&](Adjustments& sample) {
                        sample.assetsMaximum = -1;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);

        testcase(prefix + "must not change shares");
        doInvariantCheck(
            {"set must not change shares outstanding",
             "updated zero sized vault must have no assets outstanding",
             "updated zero sized vault must have no assets available"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                ac.view().update(vaultSle);
                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;
                (*sleShares)[sfOutstandingAmount] = 0;
                ac.view().update(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);
    }
    void
    testVaultDelete()
    {
        using namespace test::jtx;

        std::string const prefix = "VaultDelete: ";

        testcase(prefix + "vault deletion succeeded without deleting a vault");
        doInvariantCheck(
            {"vault deletion succeeded without deleting a vault"},
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto vaultSle = ac.view().peek(*keylet);
                if (!vaultSle)
                    return false;
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "vault deleted by a wrong transaction type");
        doInvariantCheck(
            {"vault deleted by a wrong transaction type"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto vaultSle = ac.view().peek(*keylet);
                if (!vaultSle)
                    return false;
                ac.view().erase(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_SET, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "vault operation updated more than single vault");
        doInvariantCheck(
            {"vault operation updated more than single vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const deleteVault = [&](AccountID const& accId) {
                    // We're not using vaultKeylet here as the accounts in this
                    // testcase have a sequence number of 1
                    auto const keylet = keylet::vault(accId, ac.view().seq());

                    auto vaultSle = ac.view().peek(keylet);
                    if (!BEAST_EXPECT(vaultSle))
                        return false;

                    ac.view().erase(vaultSle);

                    return true;
                };

                BEAST_EXPECT(deleteVault(A1.id()));
                BEAST_EXPECT(deleteVault(A2.id()));
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                {
                    auto [tx, _] = vault.create({.owner = A1, .asset = xrpIssue()});
                    env(tx);
                }
                {
                    auto [tx, _] = vault.create({.owner = A2, .asset = xrpIssue()});
                    env(tx);
                }
                return true;
            });

        testcase(prefix + "deleted vault must also delete shares");
        doInvariantCheck(
            {"deleted vault must also delete shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                ac.view().erase(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));

        testcase(prefix + "deleted vault must be empty");
        doInvariantCheck(
            {"deleted vault must have no shares outstanding",
             "deleted vault must have no assets outstanding",
             "deleted vault must have no assets available"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;
                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;
                ac.view().erase(vaultSle);
                ac.view().erase(sleShares);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            [&](Account const& A1, Account const& A2, Env& env) {
                Vault vault{env};
                auto [tx, keylet] = vault.create({.owner = A1, .asset = xrpIssue()});
                env(tx);
                env(vault.deposit({.depositor = A1, .id = keylet.key, .amount = XRP(10)}));
                return true;
            });

        testcase(prefix + "vault operation succeeded without modifying a vault");
        doInvariantCheck(
            {"vault operation succeeded without modifying a vault"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) { return true; },
            XRPAmount{},
            STTx{ttVAULT_DELETE, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP));
    }

    void
    testVaultDeposit(AssetType assetType)
    {
        using namespace test::jtx;

        auto const prefix = "VaultDeposit(" + assetTypeToString(assetType) + "): ";

        // no benefit to run this test with multiple asset types
        if (assetType == AssetType::XRP)
        {
            testcase(prefix + "vault operation succeeded without modifying a vault");
            doInvariantCheck(
                {"vault operation succeeded without modifying a vault"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) { return true; },
                XRPAmount{},
                STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createEmptyVault());
        }

        testcase(prefix + "deposit must change vault balance");
        doInvariantCheck(
            {"deposit must change vault balance"},
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;
                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A1.id(), 0, [](Adjustments& sample) {
                        sample.vaultAssets.reset();
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType));

        testcase(prefix + "deposit must change depositor balance");
        doInvariantCheck(
            {"deposit must change depositor balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A1.id(), 10, [&](Adjustments& sample) {
                        // cancel out the account asset change
                        sample.accountAssets->amount = -10;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit and assets outstanding must add up");
        doInvariantCheck(
            {"deposit and assets outstanding must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [&](Adjustments& sample) {
                        sample.assetsTotal = 11;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT,
                [&](STObject& tx) {
                    // prevent triggering
                    // Invariant failed: deposit must not change
                    // vault balance by more than deposited
                    // amount

                    tx[sfAmount] = XRPAmount(10);
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit assets outstanding must not exceed assets maximum");
        doInvariantCheck(
            {"deposit assets outstanding must not exceed assets maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 200, [&](Adjustments& sample) {
                        sample.assetsMaximum = 1;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_DEPOSIT, [](STObject& tx) { tx.setFieldAmount(sfAmount, XRPAmount(200)); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit must change depositor shares");
        doInvariantCheck(
            {"deposit must change depositor shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [&](Adjustments& sample) {
                        sample.accountShares.reset();
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit must change vault shares");
        doInvariantCheck(
            {"deposit must change vault shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [](Adjustments& sample) {
                        sample.sharesTotal = 0;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit must change vault and depositor balances");
        doInvariantCheck(
            {"deposit must increase vault balance", "deposit must change depositor balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A1.id(), -10, [&](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [&](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit must not change loss unrealized");
        doInvariantCheck(
            {"loss unrealized must not exceed the difference "
             "between assets outstanding and available",
             "vault transaction must not change loss unrealized"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 100, [&](Adjustments& sample) {
                        sample.lossUnrealized = 13;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit must change vault and depositor shares");
        doInvariantCheck(
            {"deposit must increase depositor shares",
             "deposit must change depositor and vault shares by equal "
             "amount",
             "deposit must not change vault balance by more than deposited "
             "amount"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [&](Adjustments& sample) {
                        sample.accountShares->amount = -5;
                        sample.sharesTotal = -10;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(5); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "deposit and vault balances must add up");
        doInvariantCheck(
            {
                "deposit must increase vault balance",
                "deposit must decrease depositor balance",
                "deposit must change vault and depositor balance by equal "
                "amount",
                "deposit and assets outstanding must add up",
                "deposit and assets available must add up",
            },
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                auto const asset = *vaultSle->at(sfAsset);

                if (!BEAST_EXPECT(
                        transferAssets(ac, asset.getIssuer(), A1, STAmount{asset, Number{10, 0}})))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [&](Adjustments& sample) {
                        sample.vaultAssets = -20;
                        sample.accountAssets->amount = 10;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "vault balances must add up");
        doInvariantCheck(
            {"deposit and assets outstanding must add up",
             "deposit and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [&](Adjustments& sample) {
                        sample.assetsTotal = 7;
                        sample.assetsAvailable = 7;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "updated shares must not exceed maximum 1");
        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;

                (*sleShares)[sfMaximumAmount] = 10;
                ac.view().update(sleShares);

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "updated shares must not exceed maximum 2");
        doInvariantCheck(
            {"updated shares must not exceed maximum"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                auto sleShares = ac.view().peek(keylet::mptIssuance((*vaultSle)[sfShareMPTID]));
                if (!BEAST_EXPECT(sleShares))
                    return false;

                (*sleShares)[sfOutstandingAmount] = maxMPTokenAmount + 1;
                ac.view().update(sleShares);
                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), 10, [](Adjustments&) {}));
            },
            XRPAmount{},
            STTx{ttVAULT_DEPOSIT, [](STObject&) {}},
            {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);
    }

    void
    testVaultWithdrawal(AssetType assetType)
    {
        using namespace test::jtx;
        auto const prefix = "VaultWithdraw(" + assetTypeToString(assetType) + ") ";

        // It's enough to test this invariant for a single asset as the
        // underlying vault is empty
        if (assetType == AssetType::XRP)
        {
            testcase(prefix + "vault operation succeeded without modifying a vault");
            doInvariantCheck(
                {"vault operation succeeded without modifying a vault"},
                [&](Account const&, Account const&, ApplyContext& ac) { return true; },
                XRPAmount{},
                STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createEmptyVault());
        }
        testcase(prefix + "vault operation succeeded without updating shares");
        doInvariantCheck(
            {"vault operation succeeded without updating shares",
             "assets available must not be greater than assets outstanding"},
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                (*vaultSle)[sfAssetsTotal] = 9;
                ac.view().update(vaultSle);
                return true;
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType));

        testcase(prefix + "withdrawal must change vault balance");
        doInvariantCheck(
            {"withdrawal must change vault balance"},
            [&](Account const& A1, Account const&, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A1.id(), 0, [](Adjustments& sample) {
                        sample.vaultAssets.reset();
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType));

        testcase(prefix + "withdrawal must change one destination balance");
        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A1.id(), 10, [&](Adjustments& sample) {
                        // cancel out the account asset change
                        sample.accountAssets->amount = -10;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [&](STObject& tx) { tx[sfAmount] = XRPAmount(10); }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType));

        testcase(prefix + "withdrawal and vault balances must add up");
        doInvariantCheck(
            {"withdrawal must change vault and destination balance by "
             "equal amount",
             "withdrawal must decrease vault balance",
             "withdrawal must increase destination balance",
             "withdrawal and assets outstanding must add up",
             "withdrawal and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                auto const asset = *vaultSle->at(sfAsset);

                if (!BEAST_EXPECT(
                        transferAssets(ac, asset.getIssuer(), A1, STAmount{asset, Number{10, 0}})))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                        sample.vaultAssets = 10;
                        sample.accountAssets->amount = -20;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "withdrawal to destination must change one destination balance");
        doInvariantCheck(
            {"withdrawal must change one destination balance"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                auto const vaultSle = ac.view().peek(*keylet);
                if (!BEAST_EXPECT(vaultSle))
                    return false;

                auto const asset = *vaultSle->at(sfAsset);

                if (!BEAST_EXPECT(adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                            *sample.vaultAssets -= 5;
                        }))))
                    return false;

                return BEAST_EXPECT(
                    transferAssets(ac, asset.getIssuer(), A1, STAmount{asset, Number{5, 0}}));
            },
            XRPAmount{},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx.setAccountID(sfDestination, test::jtx::Account{"A1"}.id());
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "withdrawal must change depositor shares");
        doInvariantCheck(
            {"withdrawal must change depositor shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;
                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                        sample.accountShares.reset();
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "withdrawal must change vault shares");
        doInvariantCheck(
            {"withdrawal must change vault shares"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;

                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), -10, [](Adjustments& sample) {
                        sample.sharesTotal = 0;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "withdrawal must decrease depositor shares");
        doInvariantCheck(
            {"withdrawal must decrease depositor shares",
             "withdrawal must change depositor and vault shares by equal "
             "amount"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;
                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                        sample.accountShares->amount = 5;
                        sample.sharesTotal = 10;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "withdrawal and vault must add up");
        doInvariantCheck(
            {"withdrawal and assets outstanding must add up",
             "withdrawal and assets available must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;
                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                        sample.assetsTotal = -15;
                        sample.assetsAvailable = -15;
                    }));
            },
            XRPAmount{},
            STTx{ttVAULT_WITHDRAW, [](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(assetType),
            TxAccount::A2);

        testcase(prefix + "withdrawal and assets outstanding must add up");
        doInvariantCheck(
            {"withdrawal and assets outstanding must add up"},
            [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                auto const keylet = vaultKeylet();
                if (!BEAST_EXPECT(keylet))
                    return false;
                return adjustVaultAndDepositor(
                    ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                        sample.assetsTotal = -7;
                    }));
            },
            XRPAmount{},
            STTx{
                ttVAULT_WITHDRAW,
                [&](STObject& tx) {
                    tx[sfAmount] = XRPAmount(10);
                    tx.setAccountID(sfDelegate, test::jtx::Account{"A1"}.id());
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
            createVault(AssetType::XRP),
            TxAccount::A2);
    }

    void
    testVaultClawback(AssetType assetType)
    {
        using namespace test::jtx;
        auto const prefix = "VaultClawback(" + assetTypeToString(assetType) + ") ";
        auto const clawbackTx = STTx{ttVAULT_CLAWBACK, [&](STObject& tx) {
                                         tx.setAccountID(sfAccount, issuer.id());
                                         tx.setAccountID(sfHolder, test::jtx::Account{"A2"}.id());
                                     }};

        if (assetType == AssetType::XRP)
        {
            testcase(prefix + "clawback XRP");
            // Not the same as below check: attempt to clawback XRP
            doInvariantCheck(
                {"clawback may only be performed by the asset issuer"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto const keylet = vaultKeylet();
                    if (!BEAST_EXPECT(keylet))
                        return false;

                    return adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), 0, [&](Adjustments& sample) {

                        }));
                },
                XRPAmount{},
                STTx{ttVAULT_CLAWBACK, [&](STObject& tx) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createVault(AssetType::XRP));

            testcase(prefix + "vault operation succeeded without modifying a vault");
            doInvariantCheck(
                {"vault operation succeeded without modifying a vault"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) { return true; },
                XRPAmount{},
                STTx{ttVAULT_CLAWBACK, [](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createEmptyVault());
        }
        else
        {
            testcase(prefix + "must change vault balance");
            doInvariantCheck(
                {"clawback must change vault balance"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto const keylet = vaultKeylet();
                    if (!BEAST_EXPECT(keylet))
                        return false;

                    auto const vaultSle = ac.view().peek(*keylet);
                    if (!BEAST_EXPECT(vaultSle))
                        return false;

                    auto const asset = *vaultSle->at(sfAsset);

                    if (!BEAST_EXPECT(transferAssets(
                            ac, asset.getIssuer(), A1, STAmount{asset, Number{1, 0}})))
                        return false;

                    return adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), -1, [&](Adjustments& sample) {
                            sample.vaultAssets.reset();
                        }));
                },
                XRPAmount{},
                STTx{
                    ttVAULT_CLAWBACK,
                    [&](STObject& tx) { tx.setAccountID(sfAccount, issuer.id()); }},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createVault(assetType));

            testcase(prefix + "may be performed only by asset issuer");
            // Not the same as above check: attempt to clawback MPT by bad
            // account
            doInvariantCheck(
                {"clawback may only be performed by the asset issuer"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto const keylet = vaultKeylet();
                    if (!BEAST_EXPECT(keylet))
                        return false;

                    return adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), 0, [&](Adjustments& sample) {
                        }));
                },
                XRPAmount{},
                STTx{
                    ttVAULT_CLAWBACK,
                    [&](STObject& tx) {
                        tx.setAccountID(sfAccount, test::jtx::Account{"A1"}.id());
                    }},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createVault(assetType));

            testcase(prefix + "must correctly change vault state");
            doInvariantCheck(
                {"clawback must decrease vault balance",
                 "clawback must decrease holder shares",
                 "clawback must change vault shares"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto const keylet = vaultKeylet();
                    if (!BEAST_EXPECT(keylet))
                        return false;

                    return adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), 10, [&](Adjustments& sample) {
                            sample.sharesTotal = 0;
                        }));
                },
                XRPAmount{},
                clawbackTx,
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createVault(assetType));

            testcase(prefix + "must change holder shares");
            doInvariantCheck(
                {"clawback must change holder shares"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto const keylet = vaultKeylet();
                    if (!BEAST_EXPECT(keylet))
                        return false;

                    return adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                            sample.accountShares.reset();
                        }));
                },
                XRPAmount{},
                clawbackTx,
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createVault(assetType));

            testcase(
                prefix +
                "clawback must change holder and vault shares by equal "
                "amount");
            doInvariantCheck(
                {"clawback must change holder and vault shares by equal "
                 "amount",
                 "clawback and assets outstanding must add up",
                 "clawback and assets available must add up"},
                [&](Account const& A1, Account const& A2, ApplyContext& ac) {
                    auto const keylet = vaultKeylet();
                    if (!BEAST_EXPECT(keylet))
                        return false;

                    return adjustVaultAndDepositor(
                        ac, *keylet, createVaultAdjustments(A2.id(), -10, [&](Adjustments& sample) {
                            sample.accountShares->amount = -8;
                            sample.assetsTotal = -7;
                            sample.assetsAvailable = -7;
                        }));
                },
                XRPAmount{},
                clawbackTx,
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED},
                createVault(assetType));
        }
    }

public:
    void
    run() override
    {
        testVaultCreate();
        testVaultSet();
        testVaultDelete();
        for (auto const assetType : assetTypes)
            testVaultDeposit(assetType);

        for (auto const assetType : assetTypes)
            testVaultWithdrawal(assetType);

        for (auto const assetType : assetTypes)
            testVaultClawback(assetType);
    }
};

BEAST_DEFINE_TESTSUITE(VaultInvariants, app, xrpl);

}  // namespace test
}  // namespace xrpl
