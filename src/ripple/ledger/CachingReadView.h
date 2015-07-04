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

#ifndef RIPPLE_LEDGER_CACHINGREADVIEW_H_INCLUDED
#define RIPPLE_LEDGER_CACHINGREADVIEW_H_INCLUDED

#include <ripple/ledger/CachedSLEs.h>
#include <ripple/ledger/ReadView.h>
#include <ripple/basics/hardened_hash.h>
#include <map>
#include <memory>
#include <mutex>

namespace ripple {

//------------------------------------------------------------------------------

/** ReadView that caches by key and hash. */
class CachingReadView
    : public ReadView
{
private:
    CachedSLEs& cache_;
    std::mutex mutable mutex_;
    DigestAwareReadView const& base_;
    std::shared_ptr<void const> hold_;
    std::unordered_map<key_type,
        std::shared_ptr<SLE const>,
            hardened_hash<>> mutable map_;

public:
    CachingReadView() = delete;
    CachingReadView (CachingReadView const&) = delete;
    CachingReadView& operator= (CachingReadView const&) = delete;

    CachingReadView(
        DigestAwareReadView const* base,
            CachedSLEs& cache,
                std::shared_ptr<void const> hold);

    CachingReadView (std::shared_ptr<
        DigestAwareReadView const> const& base,
            CachedSLEs& cache)
        : CachingReadView (&*base, cache, base)
    {
    }

    bool
    exists (Keylet const& k) const override;

    std::shared_ptr<SLE const>
    read (Keylet const& k) const override;

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

    boost::optional<key_type>
    succ (key_type const& key, boost::optional<
        key_type> last = boost::none) const override
    {
        return base_.succ(key, last);
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

};

//------------------------------------------------------------------------------

/** Wrap a DigestAwareReadView with a cache.

    Effects:

        Returns ownership of a base ReadView that is
        wrapped in a thread-safe cache.

        The returned ReadView gains a reference to
        the base.

    Postconditions:

        The base object will not be destroyed before
        the returned view is destroyed.

    The caller is responsible for ensuring that the
    `cache` object lifetime extends to the lifetime of
    the returned object.
*/
inline
std::shared_ptr<ReadView const>
makeCached (std::shared_ptr<
    DigestAwareReadView const> const& base,
        CachedSLEs& cache)
{
    return std::make_shared<
        CachingReadView const>(
            &*base, cache, base);
}

} // ripple

#endif
