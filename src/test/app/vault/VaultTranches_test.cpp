#include <test/app/vault/VaultTestBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/ter.h>
#include <test/jtx/vault.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TER.h>

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

public:
    void
    run() override
    {
        testCreateDisabled();
    }
};

BEAST_DEFINE_TESTSUITE(VaultTranches, app, xrpl);

}  // namespace xrpl
