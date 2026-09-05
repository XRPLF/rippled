#include <xrpl/ledger/helpers/DirectoryNodeEntry.h>

#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>

#include <gtest/gtest.h>
#include <ledger/EntryTestHelpers.h>

#include <cstdint>

namespace xrpl::test {

TEST(DirectoryNodeEntryTests, Constructors)
{
    EntryTestEnv e;

    expectKeylet<DirectoryNodeEntry>(
        e, keylet::ownerDir(e.alice.id()), "ownerDir(id)", e.alice.id());

    expectKeylet<DirectoryNodeEntry>(
        e, keylet::page(e.someID(), 3u), "page(root, index)", e.someID(), std::uint64_t{3});

    // The two overloads reach different keylet:: functions; a copy-paste
    // slip between them would be invisible otherwise.
    EXPECT_NE(keylet::ownerDir(e.alice.id()).key, keylet::page(e.someID(), 3u).key);
}

}  // namespace xrpl::test
