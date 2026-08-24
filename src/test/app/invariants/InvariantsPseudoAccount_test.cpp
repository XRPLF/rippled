#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>

#include <array>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsPseudoAccount_test : public InvariantsBase
{
    void
    testValidPseudoAccounts()
    {
        testcase << "valid pseudo accounts";

        using namespace jtx;

        AccountID pseudoAccountID;
        Preclose const createPseudo = [&, this](Account const& a, Account const& b, Env& env) {
            PrettyAsset const xrpAsset{xrpIssue(), 1'000'000};

            // Create vault
            Vault const vault{env};
            auto [tx, vKeylet] = vault.create({.owner = a, .asset = xrpAsset});
            env(tx);
            env.close();
            if (auto const vSle = env.le(vKeylet); BEAST_EXPECT(vSle))
            {
                pseudoAccountID = vSle->at(sfAccount);
            }

            return BEAST_EXPECT(env.le(keylet::account(pseudoAccountID)));
        };

        /* Cases to check
            "pseudo-account has 0 pseudo-account fields set"
            "pseudo-account has 2 pseudo-account fields set"
            "pseudo-account sequence changed"
            "pseudo-account flags are not set"
            "pseudo-account has a regular key"
            "pseudo-account has a sponsorship field"
        */
        struct Mod
        {
            std::string expectedFailure;
            std::function<void(SLE::pointer&)> func;
        };
        auto const mods = std::to_array<Mod>({
            {
                .expectedFailure = "pseudo-account has 0 pseudo-account fields set",
                .func =
                    [this](SLE::pointer& sle) {
                        BEAST_EXPECT(sle->at(~sfVaultID));
                        sle->at(~sfVaultID) = std::nullopt;
                    },
            },
            {
                .expectedFailure = "pseudo-account sequence changed",
                .func = [](SLE::pointer& sle) { sle->at(sfSequence) = 12345; },
            },
            {
                .expectedFailure = "pseudo-account flags are not set",
                .func = [](SLE::pointer& sle) { sle->at(sfFlags) = lsfNoFreeze; },
            },
            {
                .expectedFailure = "pseudo-account has a regular key",
                .func = [](SLE::pointer& sle) { sle->at(sfRegularKey) = Account("regular").id(); },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsoredOwnerCount) = 1; },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsoringOwnerCount) = 1; },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsoringAccountCount) = 1; },
            },
            {
                .expectedFailure = "pseudo-account has a sponsorship field",
                .func = [](SLE::pointer& sle) { sle->at(sfSponsor) = Account("sponsor").id(); },
            },
        });

        for (auto const& mod : mods)
        {
            doInvariantCheck(
                {{mod.expectedFailure}},
                [&](Account const& a1, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::account(pseudoAccountID));
                    if (!sle)
                        return false;
                    mod.func(sle);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createPseudo);
        }
        for (auto const pField : getPseudoAccountFields())
        {
            // createPseudo creates a vault, so sfVaultID will be set, and
            // setting it again will not cause an error
            if (pField == &sfVaultID)
                continue;
            doInvariantCheck(
                {{"pseudo-account has 2 pseudo-account fields set"}},
                [&](Account const& a1, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(keylet::account(pseudoAccountID));
                    if (!sle)
                        return false;

                    auto const vaultID = ~sle->at(~sfVaultID);
                    BEAST_EXPECT(vaultID && !sle->isFieldPresent(*pField));
                    sle->setFieldH256(*pField, *vaultID);

                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttACCOUNT_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createPseudo);
        }

        // Take one of the regular accounts and set the sequence to 0, which
        // will make it look like a pseudo-account
        doInvariantCheck(
            {{"pseudo-account has 0 pseudo-account fields set"},
             {"pseudo-account sequence changed"},
             {"pseudo-account flags are not set"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto sle = ac.view().peek(keylet::account(a1.id()));
                if (!sle)
                    return false;
                sle->at(sfSequence) = 0;
                ac.view().update(sle);
                return true;
            });
    }

    void
    testValidLoanBroker()
    {
        testcase << "valid loan broker";

        using namespace jtx;

        enum class Asset { XRP, IOU, MPT };
        auto const assetTypes = std::to_array({Asset::XRP, Asset::IOU, Asset::MPT});

        for (auto const assetType : assetTypes)
        {
            // Initialize with a placeholder value because there's no default
            // ctor
            auto const setupAsset =
                [&](Account const& alice, Account const& issuer, Env& env) -> PrettyAsset {
                switch (assetType)
                {
                    case Asset::IOU: {
                        PrettyAsset const iouAsset = issuer["IOU"];
                        env(trust(alice, iouAsset(1000)));
                        env(pay(issuer, alice, iouAsset(1000)));
                        env.close();
                        return iouAsset;
                    }
                    case Asset::MPT: {
                        MPTTester mptt{env, issuer, kMptInitNoFund};
                        mptt.create({.flags = tfMPTCanClawback | tfMPTCanTransfer | tfMPTCanLock});
                        PrettyAsset const mptAsset = mptt.issuanceID();
                        mptt.authorize({.account = alice});
                        env(pay(issuer, alice, mptAsset(1000)));
                        env.close();
                        return mptAsset;
                    }
                    case Asset::XRP:
                    default:
                        return PrettyAsset{xrpIssue(), 1'000'000};
                }
            };

            Keylet loanBrokerKeylet = keylet::amendments();
            Preclose const createLoanBroker =
                [&, this](Account const& alice, Account const& issuer, Env& env) {
                    auto const asset = setupAsset(alice, issuer, env);
                    loanBrokerKeylet = this->createLoanBroker(alice, env, asset);
                    return BEAST_EXPECT(env.le(loanBrokerKeylet));
                };

            // Ensure the test scenarios are set up completely. The test cases
            // will need to recompute any of these values it needs for itself
            // rather than trying to return a bunch of items
            auto setupTest = [&, this](Account const& a1, Account const&, ApplyContext& ac)
                -> std::optional<std::pair<SLE::pointer, SLE::pointer>> {
                if (loanBrokerKeylet.type != ltLOAN_BROKER)
                    return {};
                auto sleBroker = ac.view().peek(loanBrokerKeylet);
                if (!sleBroker)
                    return {};
                if (!BEAST_EXPECT(sleBroker->at(sfOwnerCount) == 0))
                    return {};
                // Need to touch sleBroker so that it is included in the
                // modified entries for the invariant to find
                ac.view().update(sleBroker);

                // The pseudo-account holds the directory, so get it
                auto const pseudoAccountID = sleBroker->at(sfAccount);
                auto const pseudoAccountKeylet = keylet::account(pseudoAccountID);
                // Strictly speaking, we don't need to load the
                // ACCOUNT_ROOT, but check anyway
                auto slePseudo = ac.view().peek(pseudoAccountKeylet);
                if (!BEAST_EXPECT(slePseudo))
                    return {};
                // Make sure the directory doesn't already exist
                auto const dirKeylet = keylet::ownerDir(pseudoAccountID);
                auto sleDir = ac.view().peek(dirKeylet);
                auto const describe = describeOwnerDir(pseudoAccountID);
                if (!sleDir)
                {
                    // Create the directory
                    BEAST_EXPECT(
                        ::xrpl::directory::createRoot(
                            ac.view(), dirKeylet, loanBrokerKeylet.key, describe) == 0);

                    sleDir = ac.view().peek(dirKeylet);
                }

                return std::make_pair(slePseudo, sleDir);
            };

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has multiple directory "
                  "pages"}},
                [&setupTest, this](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto const describe = describeOwnerDir(slePseudo->at(sfAccount));

                    BEAST_EXPECT(
                        ::xrpl::directory::insertPage(
                            ac.view(),
                            0,
                            sleDir,
                            0,
                            sleDir,
                            slePseudo->key(),
                            keylet::page(sleDir->key(), 0),
                            describe) == 1);

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has multiple indexes in "
                  "the Directory root"}},
                [&setupTest](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto indexes = sleDir->getFieldV256(sfIndexes);

                    // Put some extra garbage into the directory
                    for (auto const& key : {slePseudo->key(), sleDir->key()})
                    {
                        ::xrpl::directory::insertKey(ac.view(), sleDir, 0, false, indexes, key);
                    }

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker directory corrupt"}},
                [&setupTest](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    auto const describe = describeOwnerDir(slePseudo->at(sfAccount));
                    // Empty vector will overwrite the existing entry for the
                    // holding, if any, avoiding the "has multiple indexes"
                    // failure.
                    STVector256 indexes;

                    // Put one meaningless key into the directory
                    auto const key = keylet::account(Account("random").id()).key;
                    ::xrpl::directory::insertKey(ac.view(), sleDir, 0, false, indexes, key);

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker with zero OwnerCount has an unexpected entry in "
                  "the directory"}},
                [&setupTest](Account const& a1, Account const& a2, ApplyContext& ac) {
                    auto test = setupTest(a1, a2, ac);
                    if (!test || !test->first || !test->second)
                        return false;

                    auto slePseudo = test->first;
                    auto sleDir = test->second;
                    // Empty vector will overwrite the existing entry for the
                    // holding, if any, avoiding the "has multiple indexes"
                    // failure.
                    STVector256 indexes;

                    ::xrpl::directory::insertKey(
                        ac.view(), sleDir, 0, false, indexes, slePseudo->key());

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            doInvariantCheck(
                {{"Loan Broker sequence number decreased"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    if (loanBrokerKeylet.type != ltLOAN_BROKER)
                        return false;
                    auto sleBroker = ac.view().peek(loanBrokerKeylet);
                    if (!sleBroker)
                        return false;
                    if (!BEAST_EXPECT(sleBroker->at(sfLoanSequence) > 0))
                        return false;
                    // Need to touch sleBroker so that it is included in the
                    // modified entries for the invariant to find
                    ac.view().update(sleBroker);

                    sleBroker->at(sfLoanSequence) -= 1;

                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);

            // Test: cover available less than pseudo-account asset balance
            {
                Keylet brokerKeylet = keylet::amendments();
                Preclose const createBrokerWithCover =
                    [&, this](Account const& alice, Account const& issuer, Env& env) {
                        auto const asset = setupAsset(alice, issuer, env);
                        brokerKeylet = this->createLoanBroker(alice, env, asset);
                        if (!BEAST_EXPECT(env.le(brokerKeylet)))
                            return false;
                        env(loan_broker::coverDeposit(alice, brokerKeylet.key, asset(10)));
                        env.close();
                        return BEAST_EXPECT(env.le(brokerKeylet));
                    };

                doInvariantCheck(
                    {{"Loan Broker cover available is less than pseudo-account asset balance"}},
                    [&](Account const&, Account const&, ApplyContext& ac) {
                        auto sle = ac.view().peek(brokerKeylet);
                        if (!BEAST_EXPECT(sle))
                            return false;
                        // Pseudo-account holds 10 units, set cover to 5
                        sle->at(sfCoverAvailable) = Number(5);
                        ac.view().update(sle);
                        return true;
                    },
                    XRPAmount{},
                    STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                    {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                    createBrokerWithCover);
            }

            // Test: cover available greater than pseudo-account asset balance
            // (requires fixCleanup3_1_3)
            doInvariantCheck(
                {{"Loan Broker cover available is greater than pseudo-account asset balance"}},
                [&](Account const&, Account const&, ApplyContext& ac) {
                    auto sle = ac.view().peek(loanBrokerKeylet);
                    if (!BEAST_EXPECT(sle))
                        return false;
                    // Pseudo-account has no cover deposited; set cover
                    // higher than any incidental balance
                    sle->at(sfCoverAvailable) = Number(1'000'000);
                    ac.view().update(sle);
                    return true;
                },
                XRPAmount{},
                STTx{ttLOAN_BROKER_SET, [](STObject& tx) {}},
                {tecINVARIANT_FAILED, tefINVARIANT_FAILED},
                createLoanBroker);
        }
    }

    void
    run() override
    {
        testValidPseudoAccounts();
        testValidLoanBroker();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsPseudoAccount, app, xrpl);

}  // namespace xrpl::test
