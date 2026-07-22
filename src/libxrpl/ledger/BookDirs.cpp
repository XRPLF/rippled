#include <xrpl/ledger/BookDirs.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/ledger/helpers/DirectoryHelpers.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Indexes.h>

#include <optional>

namespace xrpl {

BookDirs::BookDirs(ReadView const& view, Book const& book)
    : view_(&view)
    , root_(keylet::page(getBookBase(book)).key)
    , nextQuality_(getQualityNext(root_))
    , key_(view_->succ(root_, nextQuality_).value_or(beast::kZero))
{
    XRPL_ASSERT(root_ != beast::kZero, "xrpl::BookDirs::BookDirs : nonzero root");
    if (key_ != beast::kZero)
    {
        if (!cdirFirst(*view_, key_, sle_, entry_, index_))
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::BookDirs::BookDirs : directory is empty");
            // LCOV_EXCL_STOP
        }
    }
}

auto
BookDirs::begin() const -> BookDirs::const_iterator
{
    auto it = BookDirs::const_iterator(*view_, root_, key_);
    if (key_ != beast::kZero)
    {
        it.nextQuality_ = nextQuality_;
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

bool
BookDirs::const_iterator::operator==(BookDirs::const_iterator const& other) const
{
    if (view_ == nullptr || other.view_ == nullptr)
        return false;

    XRPL_ASSERT(
        view_ == other.view_ && root_ == other.root_,
        "xrpl::BookDirs::const_iterator::operator== : views and roots are "
        "matching");
    return entry_ == other.entry_ && curKey_ == other.curKey_ && index_ == other.index_;
}

BookDirs::const_iterator::reference
BookDirs::const_iterator::operator*() const
{
    XRPL_ASSERT(
        index_ != beast::kZero, "xrpl::BookDirs::const_iterator::operator* : nonzero index");
    if (!cache_)
        cache_ = view_->read(keylet::offer(index_));
    return *cache_;
}

BookDirs::const_iterator&
BookDirs::const_iterator::operator++()
{
    using beast::kZero;

    XRPL_ASSERT(index_ != kZero, "xrpl::BookDirs::const_iterator::operator++ : nonzero index");
    if (!cdirNext(*view_, curKey_, sle_, entry_, index_))
    {
        if (index_ == 0)
            curKey_ = view_->succ(++curKey_, nextQuality_).value_or(kZero);

        if (index_ != 0 || curKey_ == kZero)
        {
            curKey_ = key_;
            entry_ = 0;
            index_ = kZero;
        }
        else if (!cdirFirst(*view_, curKey_, sle_, entry_, index_))
        {
            // LCOV_EXCL_START
            UNREACHABLE("xrpl::BookDirs::const_iterator::operator++ : directory is empty");
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
        index_ != beast::kZero, "xrpl::BookDirs::const_iterator::operator++(int) : nonzero index");
    const_iterator tmp(*this);
    ++(*this);
    return tmp;
}

}  // namespace xrpl
