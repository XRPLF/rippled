#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>

namespace {

struct NameSpaceEntry
{
    std::string_view name;
    char value;
};

// Every LedgerNameSpace entry, live and retired, from the protocol's single list.
constexpr std::array kLedgerNameSpaces{
#define LEDGER_NAMESPACE(n, v) NameSpaceEntry{.name = #n, .value = (v)},
#define LEDGER_NAMESPACE_RETIRED(n, v) NameSpaceEntry{.name = #n, .value = (v)},
#include <xrpl/protocol/detail/ledger_namespaces.macro>
#undef LEDGER_NAMESPACE_RETIRED
#undef LEDGER_NAMESPACE
};

}  // namespace

TEST(LedgerNameSpace, values_are_unique)
{
    for (std::size_t i = 0; i < kLedgerNameSpaces.size(); ++i)
    {
        for (std::size_t j = i + 1; j < kLedgerNameSpaces.size(); ++j)
        {
            EXPECT_NE(kLedgerNameSpaces[i].value, kLedgerNameSpaces[j].value)
                << kLedgerNameSpaces[i].name << " and " << kLedgerNameSpaces[j].name << " share '"
                << kLedgerNameSpaces[i].value << "'";
        }
    }
}
