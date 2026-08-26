#include <xrpl/ledger/helpers/OracleEntry.h>
#include <xrpl/protocol/Indexes.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(OracleEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<OracleEntry>(
        e,
        keylet::oracle(e.alice.id(), 7u),
        "oracle(account, documentID)",
        e.alice.id(),
        std::uint32_t{7});
}

}  // namespace xrpl::test
