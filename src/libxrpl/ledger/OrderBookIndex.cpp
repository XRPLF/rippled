#include <xrpl/ledger/OrderBookIndex.h>

#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STLedgerEntry.h>

#include <atomic>

namespace xrpl {

namespace {

// Operator-facing kill switch. Defaults to true; set false via setEnabled()
// to bypass the index entirely and fall back to baseline succ() iteration
// without a restart (mirrors TopOfBookCache).
std::atomic<bool> gEnabled{true};

}  // namespace

bool
OrderBookIndex::enabled() noexcept
{
    return gEnabled.load(std::memory_order_relaxed);
}

void
OrderBookIndex::setEnabled(bool on) noexcept
{
    gEnabled.store(on, std::memory_order_relaxed);
}

OrderBookIndex::OrderBookIndex(OrderBookIndex&& other)
{
    std::unique_lock lock(other.mutex_);
    books_ = std::move(other.books_);
}

OrderBookIndex
OrderBookIndex::clone() const
{
    OrderBookIndex out;
    std::shared_lock lock(mutex_);
    // Copying the map copies each BookState — a shared_ptr root (O(1), shares
    // all offer nodes) + the counter. Total O(#books).
    out.books_ = books_;
    return out;
}

void
OrderBookIndex::insertOffer(Book const& book, uint256 const& dirRoot, uint256 const& offerKey)
{
    std::unique_lock lock(mutex_);
    auto& st = books_[book];
    st.root = detail::otInsert(st.root, dirRoot, st.nextSeq++, offerKey);
    inserts_.fetch_add(1, std::memory_order_relaxed);
}

void
OrderBookIndex::deleteOffer(Book const& book, uint256 const& dirRoot, uint256 const& offerKey)
{
    std::unique_lock lock(mutex_);
    auto const it = books_.find(book);
    if (it == books_.end())
        return;
    auto const seq = detail::otFindSeq(it->second.root, dirRoot, offerKey);
    if (!seq)
        return;
    it->second.root = detail::otDelete(it->second.root, dirRoot, *seq);
    deletes_.fetch_add(1, std::memory_order_relaxed);
    if (!it->second.root)
        books_.erase(it);
}

std::vector<uint256>
OrderBookIndex::flatten(Book const& book) const
{
    std::vector<uint256> out;
    std::shared_lock lock(mutex_);
    auto const it = books_.find(book);
    if (it != books_.end())
        detail::otInorder(it->second.root, out);
    return out;
}

std::optional<uint256>
OrderBookIndex::firstOffer(Book const& book) const
{
    std::shared_lock lock(mutex_);
    auto const it = books_.find(book);
    if (it == books_.end())
        return std::nullopt;
    return detail::otFirst(it->second.root);
}

std::vector<std::pair<uint256, uint256>>
OrderBookIndex::walkBook(ReadView const& view, Book const& book)
{
    // Canonical quality-ordered enumeration, mirroring NetworkOPs::getBookPage
    // and BookTip: succ() over directory roots in [bookBase, bookEnd), then
    // cdirFirst/cdirNext across each root's pages. uTip advances to the found
    // root, so the next succ() yields the next-worse quality; a root's overflow
    // pages live outside [bookBase, bookEnd) and are reached only via sfIndexNext
    // inside cdirNext, never by succ().
    std::vector<std::pair<uint256, uint256>> out;
    uint256 const bookBase = getBookBase(book);
    uint256 const bookEnd = getQualityNext(bookBase);
    uint256 uTip = bookBase;

    for (;;)
    {
        auto const next = view.succ(uTip, bookEnd);
        if (!next)
            break;
        uint256 const dirRoot = *next;

        std::shared_ptr<SLE const> page;
        unsigned int index = 0;
        uint256 offerKey;
        if (cdirFirst(view, dirRoot, page, index, offerKey))
        {
            do
            {
                out.emplace_back(dirRoot, offerKey);
            } while (cdirNext(view, dirRoot, page, index, offerKey));
        }
        uTip = dirRoot;
    }
    return out;
}

void
OrderBookIndex::rebuildBook(ReadView const& view, Book const& book)
{
    auto const walked = walkBook(view, book);

    BookState st;
    // Inserting in walk order assigns ascending insertSeq, so in-order traversal
    // reproduces the walk exactly.
    for (auto const& [dirRoot, offerKey] : walked)
        st.root = detail::otInsert(st.root, dirRoot, st.nextSeq++, offerKey);

    std::unique_lock lock(mutex_);
    if (!st.root)
        books_.erase(book);
    else
        books_[book] = std::move(st);
    rebuilds_.fetch_add(1, std::memory_order_relaxed);
}

bool
OrderBookIndex::validateMatchesShaMap(ReadView const& view, Book const& book) const
{
    std::vector<uint256> fresh;
    for (auto const& [dirRoot, offerKey] : walkBook(view, book))
        fresh.push_back(offerKey);

    return fresh == flatten(book);
}

bool
OrderBookIndex::contains(Book const& book) const
{
    std::shared_lock lock(mutex_);
    return books_.find(book) != books_.end();
}

void
OrderBookIndex::eraseBook(Book const& book)
{
    std::unique_lock lock(mutex_);
    books_.erase(book);
}

void
OrderBookIndex::clear()
{
    std::unique_lock lock(mutex_);
    books_.clear();
}

std::size_t
OrderBookIndex::bookCount() const
{
    std::shared_lock lock(mutex_);
    return books_.size();
}

std::size_t
OrderBookIndex::offerCount(Book const& book) const
{
    std::shared_lock lock(mutex_);
    auto const it = books_.find(book);
    if (it == books_.end())
        return 0;
    return detail::otSize(it->second.root);
}

}  // namespace xrpl
