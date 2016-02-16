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

#ifndef RIPPLE_TEST_JTX_JSON_H_INCLUDED
#define RIPPLE_TEST_JTX_JSON_H_INCLUDED

#include <ripple/test/jtx/Env.h>
#include <ripple/json/json_value.h>

namespace ripple {
namespace test {
namespace jtx {

/** Inject raw JSON. */
class json
{
private:
    Json::Value jv_;

public:
    explicit
    json (std::string const&);

    explicit
    json (char const*);

    explicit
    json (Json::Value);

    template <class T>
    json (Json::StaticString const& key, T const& value)
    {
        jv_[key] = value;
    }

    template <class T>
    json (std::string const& key, T const& value)
    {
        jv_[key] = value;
    }

    void
    operator()(Env&, JTx& jt) const;
};

} // jtx
} // test
} // ripple

#endif
