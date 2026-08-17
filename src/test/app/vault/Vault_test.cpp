#include <xrpl/beast/unit_test/global_suites.h>
#include <xrpl/beast/unit_test/suite.h>

#include <algorithm>
#include <array>
#include <string_view>

namespace xrpl {

/**
 * Aggregator: running this suite ("Vault") reruns every topical Vault
 * suite in one invocation. Each member suite below remains independently
 * runnable under its own name. Declared manual so an unfiltered full test
 * run doesn't execute every case twice.
 */
class Vault_test : public beast::unit_test::Suite
{
    void
    run() override
    {
        static constexpr std::array<std::string_view, 11> kMembers{
            "VaultBugs",
            "VaultClawback",
            "VaultClosedEnded",
            "VaultDomain",
            "VaultFreeze",
            "VaultLifecycle",
            "VaultRPC",
            "VaultScale",
            "VaultShares",
            "VaultSoleShareholder",
            "VaultValidation",
        };

        for (auto const& info : beast::unit_test::globalSuites())
        {
            if (std::ranges::find(kMembers, info.name()) != kMembers.end())
                info.run(runner());
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Vault, app, xrpl);

}  // namespace xrpl
