#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/credentials.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <vector>

namespace xrpl {

class VaultTranches_test : public VaultTestBase
{
    struct TrancheArgs
    {
        std::uint32_t interestRate = 0;
        std::uint32_t subordinationMinimum = 0;
    };

    static void
    addTranches(json::Value& tx, std::vector<TrancheArgs> const& tranches)
    {
        auto& array = tx[sfTranches.jsonName];
        array = json::ValueType::Array;
        for (auto const& t : tranches)
        {
            json::Value entry;
            entry[sfTranche.jsonName][sfInterestRate.jsonName] = t.interestRate;
            if (t.subordinationMinimum != 0)
            {
                entry[sfTranche.jsonName][sfSubordinationMinimum.jsonName] = t.subordinationMinimum;
            }
            array.append(entry);
        }
    }

    void
    testCreateDisabled()
    {
        testcase("create disabled");
        using namespace test::jtx;

        Env env(*this, all_ - featureStructuredCredit);
        Account const owner{"owner"};
        env.fund(XRP(10'000), owner);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx, Ter(temDISABLED));
    }

    // Submits a VaultCreate with one sfTranches entry carrying the raw `entry` fields and
    // expects `ter`.
    void
    expectCreateMalformed(json::Value const& tranches)
    {
        using namespace test::jtx;
        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(10'000), owner);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        tx[sfTranches.jsonName] = tranches;
        env(tx, Ter(temMALFORMED));
    }

    static json::Value
    trancheEntry(std::uint32_t interestRate)
    {
        json::Value entry;
        entry[sfTranche.jsonName][sfInterestRate.jsonName] = interestRate;
        return entry;
    }

    void
    testCreate()
    {
        testcase("create");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        Vault const vault{env};
        auto [plainTx, plainKeylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        env(plainTx);
        env.close();

        auto [tx, keylet] = vault.create({.owner = owner, .asset = xrpIssue()});
        addTranches(
            tx,
            {{.interestRate = 5'000, .subordinationMinimum = 30'000},
             {.interestRate = 8'000, .subordinationMinimum = 10'000}});
        auto const parentClose = env.current()->parentCloseTime();
        env(tx);
        env.close();

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfTranches)))
            return;
        auto const& tranches = sle->getFieldArray(sfTranches);
        if (!BEAST_EXPECT(tranches.size() == 2))
            return;
        BEAST_EXPECT(tranches[0][sfInterestRate] == 5'000);
        BEAST_EXPECT(tranches[0][sfSubordinationMinimum] == 30'000);
        BEAST_EXPECT(tranches[1][sfInterestRate] == 8'000);
        BEAST_EXPECT(tranches[1][sfSubordinationMinimum] == 10'000);
        BEAST_EXPECT(sle->at(~sfTrancheAccrualTime) == parentClose.time_since_epoch().count());

        auto const equity = env.le(keylet::mptokenIssuance(sle->at(sfShareMPTID)));
        if (!BEAST_EXPECT(equity))
            return;
        for (auto const& tranche : tranches)
        {
            auto const issuance = env.le(keylet::mptokenIssuance(tranche[sfShareMPTID]));
            if (!BEAST_EXPECT(issuance))
                return;
            BEAST_EXPECT(issuance->at(sfIssuer) == sle->at(sfAccount));
            BEAST_EXPECT(issuance->getFlags() == equity->getFlags());
            BEAST_EXPECT(issuance->at(sfOutstandingAmount) == 0);
        }

        auto const plain = env.le(plainKeylet);
        auto const plainPseudo = env.le(keylet::account(plain->at(sfAccount)));
        auto const pseudo = env.le(keylet::account(sle->at(sfAccount)));
        BEAST_EXPECT(pseudo->at(sfOwnerCount) == plainPseudo->at(sfOwnerCount) + 2);
    }

    void
    testCreatePrivate()
    {
        testcase("create private");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        Account const pdOwner{"pdOwner"};
        Account const credIssuer{"credIssuer"};
        env.fund(XRP(100'000), owner, pdOwner, credIssuer);
        env.close();

        pdomain::Credentials const credentials{{.issuer = credIssuer, .credType = "member"}};
        env(pdomain::setTx(pdOwner, credentials));
        auto const domainId = pdomain::getNewDomain(env.meta());
        env.close();

        Vault const vault{env};
        auto [tx, keylet] =
            vault.create({.owner = owner, .asset = xrpIssue(), .flags = tfVaultPrivate});
        tx[sfDomainID] = to_string(domainId);
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx);
        env.close();

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfTranches)))
            return;
        auto const issuance =
            env.le(keylet::mptokenIssuance(sle->getFieldArray(sfTranches)[0][sfShareMPTID]));
        if (!BEAST_EXPECT(issuance))
            return;
        BEAST_EXPECT(issuance->isFlag(lsfMPTRequireAuth));
        BEAST_EXPECT(issuance->at(~sfDomainID) == domainId);
    }

    void
    testCreateNonTransferable()
    {
        testcase("create non-transferable");
        using namespace test::jtx;

        Env env(*this, all_);
        Account const owner{"owner"};
        env.fund(XRP(100'000), owner);
        env.close();

        Vault const vault{env};
        auto [tx, keylet] = vault.create(
            {.owner = owner, .asset = xrpIssue(), .flags = tfVaultShareNonTransferable});
        addTranches(tx, {{.interestRate = 5'000}});
        env(tx);
        env.close();

        auto const sle = env.le(keylet);
        if (!BEAST_EXPECT(sle && sle->isFieldPresent(sfTranches)))
            return;
        auto const issuance =
            env.le(keylet::mptokenIssuance(sle->getFieldArray(sfTranches)[0][sfShareMPTID]));
        if (!BEAST_EXPECT(issuance))
            return;
        BEAST_EXPECT(!issuance->isFlag(lsfMPTCanTransfer));
    }

    void
    testCreateMalformedEmpty()
    {
        testcase("create malformed: empty");
        expectCreateMalformed(json::Value{json::ValueType::Array});
    }

    void
    testCreateMalformedTooMany()
    {
        testcase("create malformed: too many");
        json::Value tranches{json::ValueType::Array};
        for (std::size_t i = 0; i <= kMaxVaultTranches; ++i)
            tranches.append(trancheEntry(1'000));
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedShareID()
    {
        testcase("create malformed: share id");
        json::Value tranches{json::ValueType::Array};
        auto entry = trancheEntry(1'000);
        entry[sfTranche.jsonName][sfShareMPTID.jsonName] = to_string(makeMptID(1, AccountID(1)));
        tranches.append(entry);
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedPrincipal()
    {
        testcase("create malformed: principal");
        json::Value tranches{json::ValueType::Array};
        auto entry = trancheEntry(1'000);
        entry[sfTranche.jsonName][sfPrincipalOutstanding.jsonName] = "100";
        tranches.append(entry);
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedRate()
    {
        testcase("create malformed: rate");
        json::Value tranches{json::ValueType::Array};
        tranches.append(trancheEntry(lending::kMaxInterestRate.value() + 1));
        expectCreateMalformed(tranches);
    }

    void
    testCreateMalformedSubordination()
    {
        testcase("create malformed: subordination");
        json::Value tranches{json::ValueType::Array};
        auto entry = trancheEntry(1'000);
        entry[sfTranche.jsonName][sfSubordinationMinimum.jsonName] = kTenthBipsPerUnity.value();
        tranches.append(entry);
        expectCreateMalformed(tranches);
    }

public:
    void
    run() override
    {
        testCreateDisabled();
        testCreate();
        testCreatePrivate();
        testCreateNonTransferable();
        testCreateMalformedEmpty();
        testCreateMalformedTooMany();
        testCreateMalformedShareID();
        testCreateMalformedPrincipal();
        testCreateMalformedRate();
        testCreateMalformedSubordination();
    }
};

BEAST_DEFINE_TESTSUITE(VaultTranches, app, xrpl);

}  // namespace xrpl
