#include <xrpl/ledger/TopOfBookCache.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

#include <optional>

namespace xrpl::test {

namespace {

// Construct a synthetic-but-consistent IOU book. The currency byte
// distinguishes books for cache lookups; pairs are XRP <-> <currency>.
Book
makeIOUBook(std::uint8_t tag)
{
    Currency c{};
    c.data()[19] = tag;
    AccountID issuer{};
    issuer.data()[19] = tag;
    Issue const inIssue{c, issuer};
    return Book{Asset{inIssue}, Asset{Issue{xrpCurrency(), xrpAccount()}}, std::nullopt};
}

// Derive the directory keylet (first-page key) for a given book at a given
// quality rate. Two distinct rates produce two distinct, prefix-comparable
// keys for the same book.
uint256
dirKey(Book const& book, std::uint64_t rate)
{
    return keylet::quality(keylet::kBook(book), rate).key;
}

}  // namespace

TEST(TopOfBookCache, EmptyCacheMisses)
{
    TopOfBookCache cache;
    EXPECT_FALSE(cache.get(makeIOUBook(1)).has_value());
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(cache.hits(), 0u);
    EXPECT_EQ(cache.misses(), 1u);
}

TEST(TopOfBookCache, RecordThenHit)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(2);
    uint256 const key = dirKey(book, 1'000'000u);

    cache.record(book, key, /*seq=*/42);

    auto const got = cache.get(book);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->firstPageKey, key);
    EXPECT_EQ(got->bestQuality, getQuality(key));
    EXPECT_EQ(got->asOfLedger, 42u);
    EXPECT_EQ(cache.hits(), 1u);
    EXPECT_EQ(cache.misses(), 0u);
}

TEST(TopOfBookCache, OnOfferInsertBetterReplacesTop)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(3);
    uint256 const worse = dirKey(book, 2'000'000u);
    uint256 const better = dirKey(book, 1'000'000u);
    // Higher rate keys sort higher (worse quality). Sanity check.
    ASSERT_LT(better, worse);

    cache.record(book, worse, 1);
    cache.onOfferInsert(book, better, 2);

    auto const got = cache.get(book);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->firstPageKey, better);
    EXPECT_EQ(got->asOfLedger, 2u);
}

TEST(TopOfBookCache, OnOfferInsertSameLeavesTop)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(4);
    uint256 const key = dirKey(book, 1'000'000u);

    cache.record(book, key, 5);
    cache.onOfferInsert(book, key, 6);

    auto const got = cache.get(book);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->firstPageKey, key);
    // asOfLedger preserved — same-quality insert is a no-op.
    EXPECT_EQ(got->asOfLedger, 5u);
}

TEST(TopOfBookCache, OnOfferInsertWorseLeavesTop)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(5);
    uint256 const best = dirKey(book, 1'000'000u);
    uint256 const worse = dirKey(book, 3'000'000u);
    ASSERT_LT(best, worse);

    cache.record(book, best, 5);
    cache.onOfferInsert(book, worse, 9);

    auto const got = cache.get(book);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->firstPageKey, best);
    EXPECT_EQ(got->asOfLedger, 5u);
}

TEST(TopOfBookCache, OnOfferInsertWithoutEntryIsNoOp)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(6);
    uint256 const key = dirKey(book, 1'000u);

    // No prior entry — we don't speculatively populate.
    cache.onOfferInsert(book, key, 1);
    EXPECT_FALSE(cache.get(book).has_value());
}

TEST(TopOfBookCache, OnOfferDeleteOfTopInvalidates)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(7);
    uint256 const top = dirKey(book, 1'000u);

    cache.record(book, top, 1);
    cache.onOfferDelete(book, top);

    EXPECT_FALSE(cache.get(book).has_value());
    EXPECT_EQ(cache.invalidations(), 1u);
}

TEST(TopOfBookCache, OnOfferDeleteOfOtherPageLeavesTop)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(8);
    uint256 const top = dirKey(book, 1'000u);
    uint256 const worsePage = dirKey(book, 4'000u);

    cache.record(book, top, 1);
    cache.onOfferDelete(book, worsePage);

    auto const got = cache.get(book);
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got->firstPageKey, top);
    EXPECT_EQ(cache.invalidations(), 0u);
}

TEST(TopOfBookCache, DistinctBooksIndependent)
{
    TopOfBookCache cache;
    Book const a = makeIOUBook(10);
    Book const b = makeIOUBook(11);

    cache.record(a, dirKey(a, 100u), 1);
    cache.record(b, dirKey(b, 200u), 2);

    EXPECT_TRUE(cache.get(a).has_value());
    EXPECT_TRUE(cache.get(b).has_value());
    EXPECT_EQ(cache.size(), 2u);

    cache.onOfferDelete(a, dirKey(a, 100u));
    EXPECT_FALSE(cache.get(a).has_value());
    EXPECT_TRUE(cache.get(b).has_value());
    EXPECT_EQ(cache.size(), 1u);
}

TEST(TopOfBookCache, InvalidateUnconditional)
{
    TopOfBookCache cache;
    Book const book = makeIOUBook(12);
    cache.record(book, dirKey(book, 100u), 1);
    cache.invalidate(book);
    EXPECT_FALSE(cache.get(book).has_value());
    EXPECT_EQ(cache.invalidations(), 1u);
    // Re-invalidating doesn't double-count.
    cache.invalidate(book);
    EXPECT_EQ(cache.invalidations(), 1u);
}

TEST(TopOfBookCache, KillSwitchToggleable)
{
    EXPECT_TRUE(TopOfBookCache::enabled());
    TopOfBookCache::setEnabled(false);
    EXPECT_FALSE(TopOfBookCache::enabled());
    TopOfBookCache::setEnabled(true);
    EXPECT_TRUE(TopOfBookCache::enabled());
}

}  // namespace xrpl::test
