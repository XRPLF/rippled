#include <xrpl/ledger/BookDirs.h>
#include <xrpl/ledger/View.h>
#include <xrpl/protocol/Indexes.h>

namespace ripple {

BookDirs::BookDirs(ReadView const& view, Book const& book)
    : view_(&view)
    , root_(keylet::page(getBookBase(book)).key)
    , next_quality_(getQualityNext(root_))
    , key_(view_->succ(root_, next_quality_).value_or(beast::zero))
{
    XRPL_ASSERT(
        root_ != beast::zero, "ripple::BookDirs::BookDirs : nonzero root");
    if (key_ != beast::zero)
    {
        if (!cdirFirst(*view_, key_, sle_, entry_, index_))
        {
            // LCOV_EXCL_START
            UNREACHABLE("ripple::BookDirs::BookDirs : directory is empty");
            // LCOV_EXCL_STOP
        }
    }
}

auto
BookDirs::begin() const -> BookDirs::const_iterator
{
    auto it = BookDirs::const_iterator(*view_, root_, key_);
    if (key_ != beast::zero)
    {
        it.next_quality_ = next_quality_;
        it.sle_ = sle_;
        it.entry_ = entry_;
        it.index_ = index_;
    }
    return it;
}

auto
BookDirs::end() const -> BookDirs::const_iterator
{
    return BookDirs::const_iterator(*view_, root_, key_);
}

beast::Journal BookDirs::const_iterator::j_ =
    beast::Journal{beast::Journal::getNullSink()};

bool
BookDirs::const_iterator::operator==(
    BookDirs::const_iterator const& other) const
{
    if (view_ == nullptr || other.view_ == nullptr)
        return false;

    XRPL_ASSERT(
        view_ == other.view_ && root_ == other.root_,
        "ripple::BookDirs::const_iterator::operator== : views and roots are "
        "matching");
    return entry_ == other.entry_ && cur_key_ == other.cur_key_ &&
        index_ == other.index_;
}

BookDirs::const_iterator::reference
BookDirs::const_iterator::operator*() const
{
    XRPL_ASSERT(
        index_ != beast::zero,
        "ripple::BookDirs::const_iterator::operator* : nonzero index");
    if (!cache_)
        cache_ = view_->read(keylet::offer(index_));
    return *cache_;
}

BookDirs::const_iterator&
BookDirs::const_iterator::operator++()
{
    using beast::zero;

    XRPL_ASSERT(
        index_ != zero,
        "ripple::BookDirs::const_iterator::operator++ : nonzero index");
    if (!cdirNext(*view_, cur_key_, sle_, entry_, index_))
    {
        if (index_ != 0 ||
            (cur_key_ =
                 view_->succ(++cur_key_, next_quality_).value_or(zero)) == zero)
        {
            cur_key_ = key_;
            entry_ = 0;
            index_ = zero;
        }
        else if (!cdirFirst(*view_, cur_key_, sle_, entry_, index_))
        {
            // LCOV_EXCL_START
            UNREACHABLE(
                "ripple::BookDirs::const_iterator::operator++ : directory is "
                "empty");
            // LCOV_EXCL_STOP
        }
    }

    cache_ = std::nullopt;
    return *this;
}

BookDirs::const_iterator
BookDirs::const_iterator::operator++(int)
{
    XRPL_ASSERT(
        index_ != beast::zero,
        "ripple::BookDirs::const_iterator::operator++(int) : nonzero index");
    const_iterator tmp(*this);
    ++(*this);
    return tmp;
}

}  // namespace ripple
