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

#ifndef RIPPLE_TEST_JTX_PATHS_H_INCLUDED
#define RIPPLE_TEST_JTX_PATHS_H_INCLUDED

#include <ripple/protocol/Issue.h>
#include <test/jtx/Env.h>
#include <type_traits>

namespace ripple {
namespace test {
namespace jtx {

/** Set Paths, SendMax on a JTx. */
class paths
{
private:
    Issue in_;
    int depth_;
    unsigned int limit_;

public:
    paths(Issue const& in, int depth = 7, unsigned int limit = 4)
        : in_(in), depth_(depth), limit_(limit)
    {
    }

    void
    operator()(Env&, JTx& jt) const;
};

//------------------------------------------------------------------------------

/** Add a path.

    If no paths are present, a new one is created.
*/
class path
{
private:
    Json::Value jv_;

public:
    path();

    template <class T, class... Args>
    explicit path(T const& t, Args const&... args);

    void
    operator()(Env&, JTx& jt) const;

private:
    Json::Value&
    create();

    void
    append_one(Account const& account);

    template <class T>
    std::enable_if_t<std::is_constructible<Account, T>::value>
    append_one(T const& t)
    {
        append_one(Account{t});
    }

    void
    append_one(IOU const& iou);

    void
    append_one(BookSpec const& book);

    template <class T, class... Args>
    void
    append(T const& t, Args const&... args);
};

template <class T, class... Args>
path::path(T const& t, Args const&... args) : jv_(Json::arrayValue)
{
    append(t, args...);
}

template <class T, class... Args>
void
path::append(T const& t, Args const&... args)
{
    append_one(t);
    if constexpr (sizeof...(args) > 0)
        append(args...);
}

}  // namespace jtx
}  // namespace test
}  // namespace ripple

#endif
