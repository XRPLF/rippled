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

#ifndef RIPPLE_LEDGER_CACHEDVIEW_H_INCLUDED
#define RIPPLE_LEDGER_CACHEDVIEW_H_INCLUDED

#include <ripple/ledger/CachedSLEs.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/basics/hardened_hash.h>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>

namespace ripple {

namespace detail {

class CachedViewImpl
    : public DigestAwareReadView
{
private:
    DigestAwareReadView const& base_;
    CachedSLEs& cache_;
    std::mutex mutable mutex_;
    std::unordered_map<key_type,
        std::shared_ptr<SLE const>,
            hardened_hash<>> mutable map_;

public:
    CachedViewImpl() = delete;
    CachedViewImpl (CachedViewImpl const&) = delete;
    CachedViewImpl& operator= (CachedViewImpl const&) = delete;

    CachedViewImpl (DigestAwareReadView const* base,
            CachedSLEs& cache)
        : base_ (*base)
        , cache_ (cache)
    {
    }

    //
    // ReadView
    //

    bool
    exists (Keylet const& k) const override;

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;

    bool
    open() const override
    {
        return base_.open();
    }

    LedgerInfo const&
    info() const override
    {
        return base_.info();
    }

    Fees const&
    fees() const override
    {
        return base_.fees();
    }

    Rules const&
    rules() const override
    {
        return base_.rules();
    }

    boost::optional<key_type>
    succ (key_type const& key, boost::optional<
        key_type> const& last = boost::none) const override
    {
        return base_.succ(key, last);
    }

    std::unique_ptr<sles_type::iter_base>
    slesBegin() const override
    {
        return base_.slesBegin();
    }

    std::unique_ptr<sles_type::iter_base>
    slesEnd() const override
    {
        return base_.slesEnd();
    }

    std::unique_ptr<sles_type::iter_base>
    slesUpperBound(uint256 const& key) const override
    {
        return base_.slesUpperBound(key);
    }

    std::unique_ptr<txs_type::iter_base>
    txsBegin() const override
    {
        return base_.txsBegin();
    }

    std::unique_ptr<txs_type::iter_base>
    txsEnd() const override
    {
        return base_.txsEnd();
    }

    bool
    txExists(key_type const& key) const override
    {
        return base_.txExists(key);
    }

    tx_type
    txRead (key_type const& key) const override
    {
        return base_.txRead(key);
    }

    //
    // DigestAwareReadView
    //

    boost::optional<digest_type>
    digest (key_type const& key) const override
    {
        return base_.digest(key);
    }

};

} // detail

/** Wraps a DigestAwareReadView to provide caching.

    @tparam Base A subclass of DigestAwareReadView
*/
template <class Base>
class CachedView
    : public detail::CachedViewImpl
{
private:
    static_assert(std::is_base_of<
        DigestAwareReadView, Base>::value, "");

    std::shared_ptr<Base const> sp_;

public:
    using base_type = Base;

    CachedView() = delete;
    CachedView (CachedView const&) = delete;
    CachedView& operator= (CachedView const&) = delete;

    CachedView (std::shared_ptr<
        Base const> const& base, CachedSLEs& cache)
        : CachedViewImpl (base.get(), cache)
        , sp_ (base)
    {
    }

    /** Returns the base type.

        @note This breaks encapsulation and bypasses the cache.
    */
    std::shared_ptr<Base const> const&
    base() const
    {
        return sp_;
    }
};

} // ripple

#endif
