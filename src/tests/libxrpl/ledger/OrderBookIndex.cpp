#include <xrpl/ledger/OrderBookIndex.h>

#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/UintTypes.h>

#include <gtest/gtest.h>

namespace xrpl::test {

namespace {

// Synthetic-but-consistent IOU book (XRP <-> tagged currency), matching the
// TopOfBookCache test helper so the two suites stay comparable.
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

// Quality-directory root key for a book at a given rate. Lower rate => lower
// key => better quality (the ordering the index relies on).
uint256
dirKey(Book const& book, std::uint64_t rate)
{
    return keylet::quality(keylet::kBook(book), rate).key;
}

// Arbitrary distinct offer key.
uint256
offerKey(std::uint8_t tag)
{
    uint256 k{};
    k.data()[0] = tag;
    return k;
}

}  // namespace

TEST(OrderBookIndex, EmptyBook)
{
    OrderBookIndex idx;
    Book const book = makeIOUBook(1);
    EXPECT_TRUE(idx.flatten(book).empty());
    EXPECT_FALSE(idx.firstOffer(book).has_value());
    EXPECT_EQ(idx.bookCount(), 0u);
    EXPECT_EQ(idx.offerCount(book), 0u);
}

TEST(OrderBookIndex, InsertWithinLevelPreservesAppendOrder)
{
    OrderBookIndex idx;
    Book const book = makeIOUBook(2);
    uint256 const lvl = dirKey(book, 1'000'000u);

    idx.insertOffer(book, lvl, offerKey(1));
    idx.insertOffer(book, lvl, offerKey(2));
    idx.insertOffer(book, lvl, offerKey(3));

    std::vector<uint256> const expect{offerKey(1), offerKey(2), offerKey(3)};
    EXPECT_EQ(idx.flatten(book), expect);
    EXPECT_EQ(idx.firstOffer(book), offerKey(1));
    EXPECT_EQ(idx.offerCount(book), 3u);
    EXPECT_EQ(idx.inserts(), 3u);
}

TEST(OrderBookIndex, LevelsOrderedBestQualityFirstRegardlessOfInsertOrder)
{
    OrderBookIndex idx;
    Book const book = makeIOUBook(3);
    uint256 const best = dirKey(book, 1'000'000u);
    uint256 const mid = dirKey(book, 2'000'000u);
    uint256 const worst = dirKey(book, 3'000'000u);
    ASSERT_LT(best, mid);
    ASSERT_LT(mid, worst);

    // Insert worst-first to prove ordering is by quality, not insertion.
    idx.insertOffer(book, worst, offerKey(30));
    idx.insertOffer(book, best, offerKey(10));
    idx.insertOffer(book, mid, offerKey(20));

    std::vector<uint256> const expect{offerKey(10), offerKey(20), offerKey(30)};
    EXPECT_EQ(idx.flatten(book), expect);
    EXPECT_EQ(idx.firstOffer(book), offerKey(10));
}

TEST(OrderBookIndex, DeletePreservesOrderAndDropsEmptyLevel)
{
    OrderBookIndex idx;
    Book const book = makeIOUBook(4);
    uint256 const a = dirKey(book, 1'000u);
    uint256 const b = dirKey(book, 2'000u);

    idx.insertOffer(book, a, offerKey(1));
    idx.insertOffer(book, a, offerKey(2));
    idx.insertOffer(book, a, offerKey(3));
    idx.insertOffer(book, b, offerKey(4));

    // Remove a middle offer: relative order of the rest is preserved.
    idx.deleteOffer(book, a, offerKey(2));
    std::vector<uint256> const expect1{offerKey(1), offerKey(3), offerKey(4)};
    EXPECT_EQ(idx.flatten(book), expect1);
    EXPECT_EQ(idx.deletes(), 1u);

    // Empty the first level: it is dropped, second becomes the front.
    idx.deleteOffer(book, a, offerKey(1));
    idx.deleteOffer(book, a, offerKey(3));
    EXPECT_EQ(idx.firstOffer(book), offerKey(4));
    EXPECT_EQ(idx.flatten(book), std::vector<uint256>{offerKey(4)});

    // Empty the book entirely: it is removed from the index.
    idx.deleteOffer(book, b, offerKey(4));
    EXPECT_TRUE(idx.flatten(book).empty());
    EXPECT_EQ(idx.bookCount(), 0u);
}

TEST(OrderBookIndex, DeleteAbsentIsNoOp)
{
    OrderBookIndex idx;
    Book const book = makeIOUBook(5);
    uint256 const lvl = dirKey(book, 1'000u);
    idx.insertOffer(book, lvl, offerKey(1));

    idx.deleteOffer(book, lvl, offerKey(99));        // absent key
    idx.deleteOffer(book, dirKey(book, 9u), offerKey(1));  // absent level
    idx.deleteOffer(makeIOUBook(6), lvl, offerKey(1));     // absent book

    EXPECT_EQ(idx.flatten(book), std::vector<uint256>{offerKey(1)});
    EXPECT_EQ(idx.deletes(), 0u);
}

TEST(OrderBookIndex, DistinctBooksIndependent)
{
    OrderBookIndex idx;
    Book const a = makeIOUBook(7);
    Book const b = makeIOUBook(8);

    idx.insertOffer(a, dirKey(a, 100u), offerKey(1));
    idx.insertOffer(b, dirKey(b, 100u), offerKey(2));
    EXPECT_EQ(idx.bookCount(), 2u);

    idx.eraseBook(a);
    EXPECT_TRUE(idx.flatten(a).empty());
    EXPECT_EQ(idx.flatten(b), std::vector<uint256>{offerKey(2)});
    EXPECT_EQ(idx.bookCount(), 1u);
}

TEST(OrderBookIndex, ClearEmptiesEverything)
{
    OrderBookIndex idx;
    Book const book = makeIOUBook(9);
    idx.insertOffer(book, dirKey(book, 1u), offerKey(1));
    idx.clear();
    EXPECT_EQ(idx.bookCount(), 0u);
    EXPECT_TRUE(idx.flatten(book).empty());
}

TEST(OrderBookIndex, KillSwitchToggleable)
{
    EXPECT_TRUE(OrderBookIndex::enabled());
    OrderBookIndex::setEnabled(false);
    EXPECT_FALSE(OrderBookIndex::enabled());
    OrderBookIndex::setEnabled(true);
    EXPECT_TRUE(OrderBookIndex::enabled());
}

}  // namespace xrpl::test
