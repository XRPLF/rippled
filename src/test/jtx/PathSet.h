//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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

#ifndef RIPPLE_LEDGER_TESTS_PATHSET_H_INCLUDED
#define RIPPLE_LEDGER_TESTS_PATHSET_H_INCLUDED

#include <ripple/basics/Log.h>
#include <ripple/protocol/TxFlags.h>
#include <test/jtx.h>

namespace ripple {
namespace test {

/** An offer exists
 */
inline
bool
isOffer (jtx::Env& env,
    jtx::Account const& account,
    STAmount const& takerPays,
    STAmount const& takerGets)
{
    bool exists = false;
    forEachItem (*env.current(), account,
        [&](std::shared_ptr<SLE const> const& sle)
        {
            if (sle->getType () == ltOFFER &&
                sle->getFieldAmount (sfTakerPays) == takerPays &&
                    sle->getFieldAmount (sfTakerGets) == takerGets)
                exists = true;
        });
    return exists;
}

class Path
{
public:
    STPath path;

    Path () = default;
    Path (Path const&) = default;
    Path& operator=(Path const&) = default;
    Path (Path&&) = default;
    Path& operator=(Path&&) = default;

    template <class First, class... Rest>
    explicit Path (First&& first, Rest&&... rest)
    {
        addHelper (std::forward<First> (first), std::forward<Rest> (rest)...);
    }
    Path& push_back (Issue const& iss);
    Path& push_back (jtx::Account const& acc);
    Path& push_back (STPathElement const& pe);
    Json::Value json () const;
 private:
    Path& addHelper (){return *this;}
    template <class First, class... Rest>
    Path& addHelper (First&& first, Rest&&... rest);
};

inline Path& Path::push_back (STPathElement const& pe)
{
    path.emplace_back (pe);
    return *this;
}

inline Path& Path::push_back (Issue const& iss)
{
    path.emplace_back (STPathElement::typeCurrency | STPathElement::typeIssuer,
        beast::zero, iss.currency, iss.account);
    return *this;
}

inline
Path& Path::push_back (jtx::Account const& account)
{
    path.emplace_back (account.id (), beast::zero, beast::zero);
    return *this;
}

template <class First, class... Rest>
Path& Path::addHelper (First&& first, Rest&&... rest)
{
    push_back (std::forward<First> (first));
    return addHelper (std::forward<Rest> (rest)...);
}

inline
Json::Value Path::json () const
{
    return path.getJson (JsonOption::none);
}

class PathSet
{
public:
    STPathSet paths;

    PathSet () = default;
    PathSet (PathSet const&) = default;
    PathSet& operator=(PathSet const&) = default;
    PathSet (PathSet&&) = default;
    PathSet& operator=(PathSet&&) = default;

    template <class First, class... Rest>
    explicit PathSet (First&& first, Rest&&... rest)
    {
        addHelper (std::forward<First> (first), std::forward<Rest> (rest)...);
    }
    Json::Value json () const
    {
        Json::Value v;
        v["Paths"] = paths.getJson (JsonOption::none);
        return v;
    }
private:
    PathSet& addHelper ()
    {
        return *this;
    }
    template <class First, class... Rest>
    PathSet& addHelper (First first, Rest... rest)
    {
        paths.emplace_back (std::move (first.path));
        return addHelper (std::move (rest)...);
    }
};

} // test
} // ripple

#endif
