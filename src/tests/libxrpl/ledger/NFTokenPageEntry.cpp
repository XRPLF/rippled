#include <xrpl/ledger/entries/NFTokenPageEntry.h>

#include <tests/libxrpl/ledger/EntryTestHelpers.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>

namespace xrpl::test {

TEST(NFTokenPageEntryTests, constructors)
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
