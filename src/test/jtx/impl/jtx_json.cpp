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

#include <BeastConfig.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/utility.h>
#include <ripple/json/json_reader.h>
#include <ripple/basics/contract.h>

namespace ripple {
namespace test {
namespace jtx {

json::json(std::string const& s)
{
    if (! Json::Reader().parse(s, jv_))
        Throw<parse_error> ("bad json");

}

json::json (char const* s)
    : json(std::string(s)){}

json::json (Json::Value jv)
    : jv_ (std::move (jv))
{
}

void
json::operator()(Env&, JTx& jt) const
{
    auto& jv = jt.jv;
    for (auto iter = jv_.begin();
            iter != jv_.end(); ++iter)
        jv[iter.key().asString()] = *iter;
}

} // jtx
} // test
} // ripple
