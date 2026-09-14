/**
 * @file NodeIdentity.cpp
 * GTest unit tests for the wallet database's node-identity storage.
 *
 * Three functions share one table, `NodeIdentity`, and the split between them
 * is what the telemetry startup order depends on: `readNodeIdentity()` only
 * reads, `storeNodeIdentity()` only writes, and `getNodeIdentity()` reads then
 * writes a fresh key when the table is empty. `xrpld` resolves its identity
 * before the Application exists and persists it later, so the store step has
 * to be callable on its own and has to be idempotent-by-read: a second run
 * must return the first run's key, not a new one.
 *
 * Each test gets its own database file in a temporary directory, so nothing
 * here depends on order or on the developer's data directory.
 */

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/server/Wallet.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

using namespace xrpl;

namespace {

/**
 * A wallet database in its own temporary directory, removed on destruction.
 *
 * `makeTestWalletDB()` creates the schema, so every fixture starts with an
 * empty `NodeIdentity` table.
 */
class TempWalletDb
{
public:
    explicit TempWalletDb(std::string const& name)
        : dir_(std::filesystem::temp_directory_path() / ("xrpl-node-identity-" + name))
    {
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);

        DatabaseCon::Setup setup;
        setup.dataDir = dir_;
        db_ = makeTestWalletDB(setup, "wallet.db", beast::Journal{beast::Journal::getNullSink()});
    }

    ~TempWalletDb()
    {
        db_.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir_, ec);
    }

    TempWalletDb(TempWalletDb const&) = delete;
    TempWalletDb&
    operator=(TempWalletDb const&) = delete;

    [[nodiscard]] DatabaseCon&
    operator*() const noexcept
    {
        return *db_;
    }

private:
    std::filesystem::path dir_;
    std::unique_ptr<DatabaseCon> db_;
};

}  // namespace

TEST(WalletNodeIdentity, store_then_read_returns_the_same_pair)
{
    // The store step exists so a key minted before the Application is built
    // can be persisted afterwards. Reading it back must give the same pair, or
    // the two halves of one run report two identities.
    TempWalletDb wallet("store-then-read");
    auto const minted = randomKeyPair(KeyType::Secp256k1);

    {
        auto db = (*wallet).checkoutDb();
        ASSERT_FALSE(readNodeIdentity(*db).has_value()) << "a fresh wallet must hold no identity";
        storeNodeIdentity(*db, minted);
    }

    auto db = (*wallet).checkoutDb();
    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->first, minted.first);
    EXPECT_EQ(stored->second, minted.second);
}

TEST(WalletNodeIdentity, store_does_not_replace_an_existing_identity)
{
    // getNodeIdentity() is the read-or-mint path and must keep the first key,
    // so a restart does not change the node's identity on the network. The
    // stored pair wins over anything a later caller offers.
    TempWalletDb wallet("no-replace");
    auto db = (*wallet).checkoutDb();

    auto const first = getNodeIdentity(*db);
    auto const other = randomKeyPair(KeyType::Secp256k1);
    ASSERT_NE(first.first, other.first)
        << "the two pairs must differ for this test to mean anything";

    storeNodeIdentity(*db, other);

    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->first, first.first);
    EXPECT_EQ(getNodeIdentity(*db).first, first.first);
}

TEST(WalletNodeIdentity, clear_then_store_installs_the_new_pair)
{
    // --newnodeid clears the row and then persists the freshly minted pair.
    // Both steps are needed: clearing alone would leave the node with no
    // stored identity at all.
    TempWalletDb wallet("clear-then-store");
    auto db = (*wallet).checkoutDb();

    auto const first = getNodeIdentity(*db);
    auto const replacement = randomKeyPair(KeyType::Secp256k1);
    ASSERT_NE(first.first, replacement.first);

    clearNodeIdentity(*db);
    EXPECT_FALSE(readNodeIdentity(*db).has_value()) << "clear must leave the table empty";

    storeNodeIdentity(*db, replacement);
    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->first, replacement.first);
    EXPECT_EQ(stored->second, replacement.second);
}

TEST(WalletNodeIdentity, get_mints_and_persists_when_the_table_is_empty)
{
    // The mint path must persist, not just return: a second call has to give
    // the same key. This is the property --newnodeid relies on to be
    // meaningful, and the one a caller that only reads would break.
    TempWalletDb wallet("mint-and-persist");
    auto db = (*wallet).checkoutDb();

    auto const minted = getNodeIdentity(*db);

    auto const stored = readNodeIdentity(*db);
    ASSERT_TRUE(stored.has_value()) << "getNodeIdentity() must persist what it mints";
    EXPECT_EQ(stored->first, minted.first);
    EXPECT_EQ(getNodeIdentity(*db).first, minted.first);
}
