#include <xrpl/ledger/Dir.h>

#include <xrpl/beast/utility/Zero.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/ledger/ReadView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>

#include <cstddef>
#include <iterator>
#include <optional>

namespace xrpl {

using const_iterator = Dir::ConstIterator;

Dir::Dir(ReadView const& view, Keylet const& key)
    : view_(&view), root_(key), sle_(view_->read(root_))
{
    if (sle_ != nullptr)
        indexes_ = &sle_->getFieldV256(sfIndexes);
}

auto
Dir::begin() const -> ConstIterator
{
    auto it = ConstIterator(*view_, root_, root_);
    if (sle_ != nullptr)
    {
        it.sle_ = sle_;
        if (!indexes_->empty())
        {
            it.indexes_ = indexes_;
            it.it_ = std::begin(*indexes_);
            it.index_ = *it.it_;
        }
    }

    return it;
}

auto
Dir::end() const -> ConstIterator
{
    return ConstIterator(*view_, root_, root_);
}

bool
const_iterator::operator==(ConstIterator const& other) const
{
    if (view_ == nullptr || other.view_ == nullptr)
        return false;

    XRPL_ASSERT(
        view_ == other.view_ && root_.key == other.root_.key,
        "xrpl::Dir::ConstIterator::operator== : views and roots are matching");
    return page_.key == other.page_.key && index_ == other.index_;
}

const_iterator::reference
const_iterator::operator*() const
{
    XRPL_ASSERT(index_ != beast::kZero, "xrpl::Dir::ConstIterator::operator* : nonzero index");
    if (!cache_)
        cache_ = view_->read(keylet::child(index_));
    return *cache_;
}

const_iterator&
const_iterator::operator++()
{
    XRPL_ASSERT(index_ != beast::kZero, "xrpl::Dir::ConstIterator::operator++ : nonzero index");
    if (++it_ != std::end(*indexes_))
    {
        index_ = *it_;
        cache_ = std::nullopt;
        return *this;
    }

    return nextPage();
}

const_iterator
const_iterator::operator++(int)
{
    XRPL_ASSERT(
        index_ != beast::kZero, "xrpl::Dir::ConstIterator::operator++(int) : nonzero index");
    ConstIterator tmp(*this);
    ++(*this);
    return tmp;
}

const_iterator&
const_iterator::nextPage()
{
    auto const next = sle_->getFieldU64(sfIndexNext);
    if (next == 0)
    {
        page_.key = root_.key;
        index_ = beast::kZero;
    }
    else
    {
        page_ = keylet::page(root_, next);
        sle_ = view_->read(page_);
        XRPL_ASSERT(sle_, "xrpl::Dir::ConstIterator::nextPage : non-null SLE");
        indexes_ = &sle_->getFieldV256(sfIndexes);
        if (indexes_->empty())
        {
            index_ = beast::kZero;
        }
        else
        {
            it_ = std::begin(*indexes_);
            index_ = *it_;
        }
    }
    cache_ = std::nullopt;
    return *this;
}

std::size_t
const_iterator::pageSize()
{
    return indexes_->size();
}

}  // namespace xrpl
