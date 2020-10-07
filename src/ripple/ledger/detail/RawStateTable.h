//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_LEDGER_RAWSTATETABLE_H_INCLUDED
#define RIPPLE_LEDGER_RAWSTATETABLE_H_INCLUDED

#include <ripple/ledger/RawView.h>
#include <ripple/ledger/ReadView.h>

#include <boost/container/pmr/monotonic_buffer_resource.hpp>
#include <boost/container/pmr/polymorphic_allocator.hpp>

#include <map>
#include <utility>

namespace ripple {
namespace detail {

// Helper class that buffers raw modifications
class RawStateTable
{
public:
    using key_type = ReadView::key_type;
    // Initial size for the monotonic_buffer_resource used for allocations
    // The size was chosen from the old `qalloc` code (which this replaces).
    // It is unclear how the size initially chosen in qalloc.
    static constexpr size_t initialBufferSize = kilobytes(256);

    RawStateTable()
        : monotonic_resource_{std::make_unique<
              boost::container::pmr::monotonic_buffer_resource>(
              initialBufferSize)}
        , items_{monotonic_resource_.get()} {};

    RawStateTable(RawStateTable const& rhs)
        : monotonic_resource_{std::make_unique<
              boost::container::pmr::monotonic_buffer_resource>(
              initialBufferSize)}
        , items_{rhs.items_, monotonic_resource_.get()}
        , dropsDestroyed_{rhs.dropsDestroyed_} {};

    RawStateTable(RawStateTable&&) = default;

    RawStateTable&
    operator=(RawStateTable&&) = delete;
    RawStateTable&
    operator=(RawStateTable const&) = delete;

    void
    apply(RawView& to) const;

    bool
    exists(ReadView const& base, Keylet const& k) const;

    boost::optional<key_type>
    succ(
        ReadView const& base,
        key_type const& key,
        boost::optional<key_type> const& last) const;

    void
    erase(std::shared_ptr<SLE> const& sle);

    void
    insert(std::shared_ptr<SLE> const& sle);

    void
    replace(std::shared_ptr<SLE> const& sle);

    std::shared_ptr<SLE const>
    read(ReadView const& base, Keylet const& k) const;

    void
    destroyXRP(XRPAmount const& fee);

    std::unique_ptr<ReadView::sles_type::iter_base>
    slesBegin(ReadView const& base) const;

    std::unique_ptr<ReadView::sles_type::iter_base>
    slesEnd(ReadView const& base) const;

    std::unique_ptr<ReadView::sles_type::iter_base>
    slesUpperBound(ReadView const& base, uint256 const& key) const;

private:
    enum class Action {
        erase,
        insert,
        replace,
    };

    class sles_iter_impl;

    struct sleAction
    {
        Action action;
        std::shared_ptr<SLE> sle;

        // Constructor needed for emplacement in std::map
        sleAction(Action action_, std::shared_ptr<SLE> const& sle_)
            : action(action_), sle(sle_)
        {
        }
    };

    // Use the boost pmr functionality instead of the c++-17 standard pmr
    // functions b/c clang does not support pmr yet (as-of 9/2020)
    using items_t = std::map<
        key_type,
        sleAction,
        std::less<key_type>,
        boost::container::pmr::polymorphic_allocator<
            std::pair<const key_type, sleAction>>>;
    // monotonic_resource_ must outlive `items_`. Make a pointer so it may be
    // easily moved.
    std::unique_ptr<boost::container::pmr::monotonic_buffer_resource>
        monotonic_resource_;
    items_t items_;

    XRPAmount dropsDestroyed_{0};
};

}  // namespace detail
}  // namespace ripple

#endif
