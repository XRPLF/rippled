#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/vault.h>
#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/Rules.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>
#include <xrpl/tx/applySteps.h>
#include <xrpl/tx/invariants/DirectoryInvariant.h>
#include <xrpl/tx/invariants/PermissionedDEXInvariant.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class InvariantsPermissioned_test : public InvariantsBase
{
    FeatureBitset const all_{test::jtx::testableAmendments()};

    void
    testPermissionedDomainInvariants(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const fixEnabled = features[fixCleanup3_1_3];
        std::initializer_list<TER> const badTers = {tecINVARIANT_FAILED, tecINVARIANT_FAILED};
        std::initializer_list<TER> const failTers = {tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        testcase << "PermissionedDomain" + std::string(fixEnabled ? " fix" : "");

        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain with no rules."}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                return createPermissionedDomain(ac, a1, a2, 0).get();
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 2";

        static constexpr auto kTooBig = kMaxPermissionedDomainCredentialsArraySize + 1;
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain bad credentials size " + std::to_string(kTooBig)}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                return !!createPermissionedDomain(ac, a1, a2, kTooBig);
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 3";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't sorted"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto slePd = createPermissionedDomain(ac, a1, a2, 0);

                STArray credentials(sfAcceptedCredentials, 2);
                for (std::size_t n = 0; n < 2; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, a2);
                    auto credType = std::string("cred_type") + std::to_string(9 - n);
                    cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                    credentials.pushBack(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().update(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain 4";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't unique"}},
            [](Account const& a1, Account const& a2, ApplyContext& ac) {
                auto slePd = createPermissionedDomain(ac, a1, a2, 0);

                STArray credentials(sfAcceptedCredentials, 2);
                for (std::size_t n = 0; n < 2; ++n)
                {
                    auto cred = STObject::makeInnerObject(sfCredential);
                    cred.setAccountID(sfIssuer, a2);
                    cred.setFieldVL(sfCredentialType, Slice("cred_type", 9));
                    credentials.pushBack(std::move(cred));
                }
                slePd->setFieldArray(sfAcceptedCredentials, credentials);
                ac.view().update(slePd);
                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 1";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain with no rules."}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD with empty rules
                {
                    STArray const credentials(sfAcceptedCredentials, 2);
                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 2";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain bad credentials size " + std::to_string(kTooBig)}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, kTooBig);

                    for (std::size_t n = 0; n < kTooBig; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, a2);
                        auto credType = "cred_type2" + std::to_string(n);
                        cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                        credentials.pushBack(std::move(cred));
                    }

                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 3";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't sorted"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    for (std::size_t n = 0; n < 2; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, a2);
                        auto credType = std::string("cred_type2") + std::to_string(9 - n);
                        cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                        credentials.pushBack(std::move(cred));
                    }

                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        testcase << "PermissionedDomain Set 4";
        doInvariantCheck(
            makeEnv(features),
            {{"permissioned domain credentials aren't unique"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                // create PD
                auto slePd = createPermissionedDomain(ac, a1, a2);

                // update PD
                {
                    STArray credentials(sfAcceptedCredentials, 2);
                    for (std::size_t n = 0; n < 2; ++n)
                    {
                        auto cred = STObject::makeInnerObject(sfCredential);
                        cred.setAccountID(sfIssuer, a2);
                        cred.setFieldVL(sfCredentialType, Slice("cred_type", 9));
                        credentials.pushBack(std::move(cred));
                    }
                    slePd->setFieldArray(sfAcceptedCredentials, credentials);
                    ac.view().update(slePd);
                }

                return true;
            },
            XRPAmount{},
            STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
            fixEnabled ? failTers : badTers);

        std::initializer_list<TER> const goodTers = {tesSUCCESS, tesSUCCESS};

        std::vector<std::string> const badMoreThan1{
            {"transaction affected more than 1 permissioned domain entry."}};
        std::vector<std::string> const emptyV;
        std::vector<std::string> const badNoDomains{{"no domain objects affected by"}};
        std::vector<std::string> const badNotDeleted{
            {"domain object modified, but not deleted by "}};
        std::vector<std::string> const badDeleted{{"domain object deleted by"}};
        std::vector<std::string> const badTx{
            {"domain object(s) affected by an unauthorized transaction."}};

        {
            testcase << "PermissionedDomain set 2 domains ";
            doInvariantCheck(
                makeEnv(features),
                fixEnabled ? badMoreThan1 : emptyV,
                [](Account const& a1, Account const& a2, ApplyContext& ac) {
                    createPermissionedDomain(ac, a1, a2);
                    createPermissionedDomain(ac, a1, a2, 2, 11);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain del 2 domains";

            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? badMoreThan1 : emptyV,
                [&pd1, &pd2](Account const&, Account const&, ApplyContext& ac) {
                    auto sle1 = ac.view().peek({ltPERMISSIONED_DOMAIN, pd1});
                    auto sle2 = ac.view().peek({ltPERMISSIONED_DOMAIN, pd2});
                    ac.view().erase(sle1);
                    ac.view().erase(sle2);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_DELETE, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain set 0 domains ";
            doInvariantCheck(
                makeEnv(features),
                fixEnabled ? badNoDomains : emptyV,
                [](Account const&, Account const&, ApplyContext&) { return true; },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
                fixEnabled ? badTers : goodTers);
        }

        {
            testcase << "PermissionedDomain del 0 domains";

            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? badNoDomains : emptyV,
                [](Account const&, Account const&, ApplyContext&) { return true; },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_DELETE, [](STObject&) {}},
                fixEnabled ? badTers : goodTers);
        }

        {
            testcase << "PermissionedDomain set, delete domain";

            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? badDeleted : emptyV,
                [&pd1](Account const&, Account const&, ApplyContext& ac) {
                    auto sle1 = ac.view().peek({ltPERMISSIONED_DOMAIN, pd1});
                    ac.view().erase(sle1);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_SET, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain del, create domain ";
            doInvariantCheck(
                makeEnv(features),
                fixEnabled ? badNotDeleted : emptyV,
                [](Account const& a1, Account const& a2, ApplyContext& ac) {
                    createPermissionedDomain(ac, a1, a2);
                    return true;
                },
                XRPAmount{},
                STTx{ttPERMISSIONED_DOMAIN_DELETE, [](STObject&) {}},
                fixEnabled ? failTers : goodTers);
        }

        {
            testcase << "PermissionedDomain invalid tx";

            doInvariantCheck(
                fixEnabled ? badTx : emptyV,
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    createPermissionedDomain(ac, a1, a2);
                    return true;
                },
                XRPAmount{},
                STTx{ttPAYMENT, [](STObject&) {}},
                failTers);
        }
    }

    void
    testPermissionedDEX(FeatureBitset features)
    {
        using namespace test::jtx;

        bool const fixEnabled = features[fixCleanup3_1_3];

        testcase << "PermissionedDEX" + std::string(fixEnabled ? " fix" : "");

        doInvariantCheck(
            makeEnv(features),
            {{"domain doesn't exist"}},
            [](Account const& a1, Account const&, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(a1.id(), SeqProxy::rawSequence(10));
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, a1);
                sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{
                ttOFFER_CREATE,
                [](STObject& tx) {
                    tx.setFieldH256(
                        sfDomainID,
                        uint256{"F10D0CC9A0F9A3CBF585B80BE09A186483668FDBDD39AA7E33"
                                "70F3649CE134E5"});
                    Account const a1{"A1"};
                    tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                    tx.setFieldAmount(sfTakerGets, XRP(1));
                }},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // missing domain ID in offer object
        doInvariantCheck(
            makeEnv(features),
            {{"hybrid offer is malformed"}},
            [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                auto sleOffer = std::make_shared<SLE>(offerKey);
                sleOffer->setAccountID(sfAccount, a2);
                sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFlag(lsfHybrid);

                STArray bookArr;
                bookArr.pushBack(STObject::makeInnerObject(sfBook));
                sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                ac.view().insert(sleOffer);
                return true;
            },
            XRPAmount{},
            STTx{ttOFFER_CREATE, [&](STObject&) {}},
            {tecINVARIANT_FAILED, tecINVARIANT_FAILED});

        // more than one entry in sfAdditionalBooks
        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"hybrid offer is malformed"}},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFlag(lsfHybrid);
                    sleOffer->setFieldH256(sfDomainID, pd1);

                    STArray bookArr;
                    bookArr.pushBack(STObject::makeInnerObject(sfBook));
                    bookArr.pushBack(STObject::makeInnerObject(sfBook));
                    sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{ttOFFER_CREATE, [&](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        // empty sfAdditionalBooks (size 0)
        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                fixEnabled ? std::vector<std::string>{{"hybrid offer is malformed"}}
                           : std::vector<std::string>{},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFlag(lsfHybrid);
                    sleOffer->setFieldH256(sfDomainID, pd1);

                    STArray const bookArr;  // empty array, size 0
                    sleOffer->setFieldArray(sfAdditionalBooks, bookArr);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{ttOFFER_CREATE, [&](STObject&) {}},
                fixEnabled ? std::initializer_list<TER>{tecINVARIANT_FAILED, tecINVARIANT_FAILED}
                           : std::initializer_list<TER>{tesSUCCESS, tesSUCCESS});
        }

        // hybrid offer missing sfAdditionalBooks
        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"hybrid offer is malformed"}},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFlag(lsfHybrid);
                    sleOffer->setFieldH256(sfDomainID, pd1);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{ttOFFER_CREATE, [&](STObject&) {}},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"transaction consumed wrong domains"}},
                [&pd1](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    sleOffer->setFieldH256(sfDomainID, pd1);
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttOFFER_CREATE,
                    [&pd2, &a1](STObject& tx) {
                        tx.setFieldH256(sfDomainID, pd2);
                        tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                        tx.setFieldAmount(sfTakerGets, XRP(1));
                    }},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }

        {
            Env env1(*this, features);

            Account const a1{"A1"};
            Account const a2{"A2"};
            env1.fund(XRP(1000), a1, a2);
            env1.close();

            [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env1, a1, a2);
            env1.close();

            doInvariantCheck(
                std::move(env1),
                a1,
                a2,
                {{"domain transaction affected regular offers"}},
                [&](Account const& a1, Account const& a2, ApplyContext& ac) {
                    Keylet const offerKey = keylet::offer(a2.id(), SeqProxy::rawSequence(10));
                    auto sleOffer = std::make_shared<SLE>(offerKey);
                    sleOffer->setAccountID(sfAccount, a2);
                    sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                    sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                    ac.view().insert(sleOffer);
                    return true;
                },
                XRPAmount{},
                STTx{
                    ttOFFER_CREATE,
                    [&](STObject& tx) {
                        Account const a1{"A1"};
                        tx.setFieldH256(sfDomainID, pd1);
                        tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                        tx.setFieldAmount(sfTakerGets, XRP(1));
                    }},
                {tecINVARIANT_FAILED, tecINVARIANT_FAILED});
        }
    }

    void
    testPermissionedDEXDeletedOfferFallback()
    {
        using namespace test::jtx;

        testcase << "PermissionedDEX deleted offer after";

        // Tx is OfferCreate on pd2. Tracking pd1 fails the invariant iff that
        // domain lands in the set finalize consults.
        auto const check =
            [this](FeatureBitset features, bool const isDelete, bool const expectInvariantFailure) {
                Env env(*this, features);

                Account const a1{"A1"};
                Account const a2{"A2"};
                env.fund(XRP(1000), a1, a2);
                env.close();

                [[maybe_unused]] auto [seq1, pd1] = createPermissionedDomainEnv(env, a1, a2);
                [[maybe_unused]] auto [seq2, pd2] = createPermissionedDomainEnv(env, a1, a2);
                env.close();

                auto sleOffer =
                    std::make_shared<SLE>(keylet::offer(a2.id(), SeqProxy::rawSequence(10)));
                sleOffer->setAccountID(sfAccount, a2);
                sleOffer->setFieldAmount(sfTakerPays, a1["USD"](10));
                sleOffer->setFieldAmount(sfTakerGets, XRP(1));
                sleOffer->setFieldH256(sfDomainID, pd1);

                CurrentTransactionRulesGuard const rulesGuard(env.current()->rules());

                ValidPermissionedDEX invariant;
                invariant.visitEntry(isDelete, nullptr, sleOffer);

                STTx const tx{ttOFFER_CREATE, [&pd2, &a1](STObject& tx) {
                                  tx.setFieldH256(sfDomainID, pd2);
                                  tx.setFieldAmount(sfTakerPays, a1["USD"](10));
                                  tx.setFieldAmount(sfTakerGets, XRP(1));
                              }};

                test::StreamSink sink{beast::Severity::Warning};
                beast::Journal const jlog{sink};
                bool const passed =
                    invariant.finalize(tx, tesSUCCESS, XRPAmount{}, *env.current(), jlog);
                BEAST_EXPECT(passed != expectInvariantFailure);
                if (expectInvariantFailure)
                {
                    BEAST_EXPECT(
                        sink.messages().str().contains("transaction consumed wrong domains"));
                }
                else
                {
                    BEAST_EXPECT(sink.messages().str().empty());
                }
            };

        auto const pre = all_ - fixCleanup3_4_0;
        auto const post = all_;

        // after == offer on pd1
        // pre-340: domainsOld_ (delete still inserted) → fail
        check(pre, true, true);
        // post-340: isDelete → only domainsOld_ → pass; !isDelete → domains_ → fail
        check(post, true, false);
        check(post, false, true);
    }

    void
    testBookDirectoryExchangeRate()
    {
        using namespace test::jtx;
        testcase << "book directory exchange rate";

        auto const getBookRootKey = [](Account const& account, std::uint64_t quality) {
            Book const book{xrpIssue(), account["USD"], std::nullopt};
            return keylet::quality(keylet::book(book), quality);
        };

        // Root book-directory pages carry exchange-rate metadata that must
        // match the quality encoded in the directory key.
        auto const makeRootPage = [](Keylet const& dir, std::uint64_t exchangeRate) {
            auto sleDir = std::make_shared<SLE>(dir);
            sleDir->setFieldH256(sfRootIndex, dir.key);
            STVector256 indexes;
            indexes.pushBack(uint256{1});
            sleDir->setFieldV256(sfIndexes, indexes);
            sleDir->setFieldU64(sfExchangeRate, exchangeRate);
            return sleDir;
        };

        // Child pages do not carry quality metadata; they only point back to
        // the root directory.
        auto const makeChildPage = [](Keylet const& rootDir) {
            auto sleDir = std::make_shared<SLE>(keylet::page(rootDir, 1));
            sleDir->setFieldH256(sfRootIndex, rootDir.key);
            STVector256 indexes;
            indexes.pushBack(uint256{2});
            sleDir->setFieldV256(sfIndexes, indexes);
            return sleDir;
        };

        auto const makeOfferCreateTx = [] {
            return STTx{ttOFFER_CREATE, [](STObject& tx) {
                            Account const account{"A1"};
                            tx.setFieldAmount(sfTakerPays, XRP(1));
                            tx.setFieldAmount(sfTakerGets, account["USD"](1));
                        }};
        };
        std::initializer_list<TER> const failTers = {tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        // Creating a root book directory with mismatched exchange-rate
        // metadata violates the invariant.
        doInvariantCheck(
            {{"book directory exchange rate does not match directory quality"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const directoryQuality = STAmount::kURateOne;
                auto const dir = getBookRootKey(a1, directoryQuality);
                ac.view().insert(makeRootPage(dir, directoryQuality + 1));
                return true;
            },
            XRPAmount{},
            makeOfferCreateTx(),
            failTers);

        // A new child page must point to an existing root page.
        doInvariantCheck(
            {{"book directory root missing"}},
            [&](Account const& a1, Account const&, ApplyContext& ac) {
                auto const directoryQuality = STAmount::kURateOne;
                auto const rootDir = getBookRootKey(a1, directoryQuality);
                // Insert only the child page.  It points at rootDir, but the
                // corresponding root page is intentionally missing.
                ac.view().insert(makeChildPage(rootDir));
                return true;
            },
            XRPAmount{},
            makeOfferCreateTx(),
            failTers);

        // Legacy bad-root tolerance:
        // - The view contains a pre-existing root page with bad sfExchangeRate
        //   metadata.
        // - The simulated transaction only creates a child page pointing to
        //   that root.
        // - The invariant must pass because this transaction did not create
        //   the bad root, only adding a child page.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            env.fund(XRP(1000), a1);
            env.close();

            OpenView view{*env.current()};
            auto const directoryQuality = STAmount::kURateOne;
            auto const rootDir = getBookRootKey(a1, directoryQuality);
            view.rawInsert(makeRootPage(rootDir, directoryQuality + 1));

            ValidBookDirectory invariant;
            invariant.visitEntry(false, nullptr, makeChildPage(rootDir));

            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};
            BEAST_EXPECT(
                invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
        }

        // A bad root is rejected when added, ignored when a legacy bad root is
        // modified without changing sfRootIndex or deleted, and checked when a
        // modified directory changes sfRootIndex.
        {
            Env env{*this, all_};
            Account const a1{"A1"};
            env.fund(XRP(1000), a1);
            env.close();

            OpenView view{*env.current()};
            auto const directoryQuality = STAmount::kURateOne;
            auto const rootDir = getBookRootKey(a1, directoryQuality);
            auto const missingRootDir = getBookRootKey(a1, directoryQuality + 1);
            auto const badRoot = makeRootPage(rootDir, directoryQuality + 1);
            view.rawInsert(badRoot);

            test::StreamSink sink{beast::Severity::Warning};
            beast::Journal const jlog{sink};

            {
                // add
                ValidBookDirectory invariant;
                invariant.visitEntry(false, nullptr, badRoot);

                BEAST_EXPECT(
                    !invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
            }
            {
                // modify (without changing the sfRootIndex)
                ValidBookDirectory invariant;
                invariant.visitEntry(false, badRoot, badRoot);

                BEAST_EXPECT(
                    invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
            }
            {
                // modify (changing sfRootIndex to a missing root)
                auto const childBefore = makeChildPage(rootDir);
                auto const childAfter = std::make_shared<SLE>(*childBefore, childBefore->key());
                childAfter->setFieldH256(sfRootIndex, missingRootDir.key);

                ValidBookDirectory invariant;
                invariant.visitEntry(false, childBefore, childAfter);

                test::StreamSink missingRootSink{beast::Severity::Warning};
                beast::Journal const missingRootJlog{missingRootSink};
                BEAST_EXPECT(!invariant.finalize(
                    makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, missingRootJlog));
                BEAST_EXPECT(
                    missingRootSink.messages().str().contains("book directory root missing"));
            }
            {
                // delete
                view.rawErase(badRoot);
                BEAST_EXPECT(!view.exists(rootDir));

                ValidBookDirectory invariant;
                invariant.visitEntry(true, badRoot, badRoot);
                BEAST_EXPECT(
                    invariant.finalize(makeOfferCreateTx(), tesSUCCESS, XRPAmount{}, view, jlog));
            }
        }
    }

    static SLE::pointer
    createPermissionedDomain(
        ApplyContext& ac,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::uint32_t numCreds = 2,
        std::uint32_t seq = 10)
    {
        Keylet const pdKeylet = keylet::permissionedDomain(a1.id(), SeqProxy::rawSequence(seq));
        auto sle = std::make_shared<SLE>(pdKeylet);

        sle->setAccountID(sfOwner, a1);
        sle->setFieldU32(sfSequence, seq);

        if (numCreds != 0u)
        {
            // This array is sorted naturally, but if you are going to change
            // this behavior, don't forget to use credentials::makeSorted
            STArray credentials(sfAcceptedCredentials, numCreds);
            for (std::size_t n = 0; n < numCreds; ++n)
            {
                auto cred = STObject::makeInnerObject(sfCredential);
                cred.setAccountID(sfIssuer, a2);
                auto credType = "cred_type" + std::to_string(n);
                cred.setFieldVL(sfCredentialType, Slice(credType.c_str(), credType.size()));
                credentials.pushBack(std::move(cred));
            }
            sle->setFieldArray(sfAcceptedCredentials, credentials);
        }

        ac.view().insert(sle);
        return sle;
    }

    static std::pair<std::uint32_t, uint256>
    createPermissionedDomainEnv(
        test::jtx::Env& env,
        test::jtx::Account const& a1,
        test::jtx::Account const& a2,
        std::uint32_t numCreds = 2)
    {
        using namespace test::jtx;

        pdomain::Credentials credentials;

        for (std::size_t n = 0; n < numCreds; ++n)
        {
            auto credType = "cred_type" + std::to_string(n);
            credentials.push_back({.issuer = a2, .credType = credType});
        }

        std::uint32_t const seq = env.seq(a1);
        env(pdomain::setTx(a1, credentials));
        uint256 const key = pdomain::getNewDomain(env.meta());

        return {seq, key};
    }

    void
    run() override
    {
        testPermissionedDomainInvariants(all_);
        testPermissionedDomainInvariants(all_ - fixCleanup3_1_3);
        testPermissionedDEX(all_);
        testPermissionedDEX(all_ - fixCleanup3_1_3);
        testPermissionedDEXDeletedOfferFallback();
        testBookDirectoryExchangeRate();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsPermissioned, app, xrpl);

}  // namespace xrpl::test
