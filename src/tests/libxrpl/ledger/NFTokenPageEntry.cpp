#include <xrpl/ledger/helpers/NFTokenPageEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

namespace xrpl::test {

TEST(NFTokenPageEntryTests, Constructors)
{
    EntryTestEnv e;

    Keylet const pageMin = keylet::nftokenPageMin(e.alice.id());

    expectKeylet<NFTokenPageEntry>(
        e,
        keylet::nftokenPage(pageMin, e.someID()),
        "nftokenPage(page, token)",
        pageMin,
        e.someID());
}

}  // namespace xrpl::test
