//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2015 Ripple Labs Inc.

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
#include <ripple/app/misc/ValidatorList.h>
#include <ripple/basics/Slice.h>
#include <ripple/basics/StringUtilities.h>
#include <boost/regex.hpp>

namespace ripple {

ValidatorList::ValidatorList (beast::Journal j)
    : j_ (j)
{
}

boost::optional<std::string>
ValidatorList::member (PublicKey const& identity) const
{
    std::lock_guard <std::mutex> sl (mutex_);

    auto ret = ephemeral_.find (identity);

    if (ret != ephemeral_.end())
        return ret->second;

    ret = permanent_.find (identity);

    if (ret != permanent_.end())
        return ret->second;

    return boost::none;
}

bool
ValidatorList::trusted (PublicKey const& identity) const
{
    return static_cast<bool> (member(identity));
}

bool
ValidatorList::insertEphemeralKey (
    PublicKey const& identity,
    std::string const& comment)
{
    std::lock_guard <std::mutex> sl (mutex_);

    if (permanent_.find (identity) != permanent_.end())
    {
        JLOG (j_.error()) <<
            toBase58 (TokenType::TOKEN_NODE_PUBLIC, identity) <<
            ": ephemeral key exists in permanent table!";
        return false;
    }

    return ephemeral_.emplace (identity, comment).second;
}

bool
ValidatorList::removeEphemeralKey (
    PublicKey const& identity)
{
    std::lock_guard <std::mutex> sl (mutex_);
    return ephemeral_.erase (identity);
}

bool
ValidatorList::insertPermanentKey (
    PublicKey const& identity,
    std::string const& comment)
{
    std::lock_guard <std::mutex> sl (mutex_);

    if (ephemeral_.find (identity) != ephemeral_.end())
    {
        JLOG (j_.error()) <<
            toBase58 (TokenType::TOKEN_NODE_PUBLIC, identity) <<
            ": permanent key exists in ephemeral table!";
        return false;
    }

    return permanent_.emplace (identity, comment).second;
}

bool
ValidatorList::removePermanentKey (
    PublicKey const& identity)
{
    std::lock_guard <std::mutex> sl (mutex_);
    return permanent_.erase (identity);
}

std::size_t
ValidatorList::size () const
{
    std::lock_guard <std::mutex> sl (mutex_);
    return permanent_.size () + ephemeral_.size ();
}

void
ValidatorList::for_each (
    std::function<void(PublicKey const&, std::string const&, bool)> func) const
{
    std::lock_guard <std::mutex> sl (mutex_);

    for (auto const& v : permanent_)
        func (v.first, v.second, false);
    for (auto const& v : ephemeral_)
        func (v.first, v.second, true);
}

bool
ValidatorList::load (
    Section const& validators)
{
    static boost::regex const re (
        "[[:space:]]*"            // skip leading whitespace
        "([[:alnum:]]+)"          // node identity
        "(?:"                     // begin optional comment block
        "[[:space:]]+"            // (skip all leading whitespace)
        "(?:"                     // begin optional comment
        "(.*[^[:space:]]+)"       // the comment
        "[[:space:]]*"            // (skip all trailing whitespace)
        ")?"                      // end optional comment
        ")?"                      // end optional comment block
    );

    JLOG (j_.debug()) <<
        "Loading configured validators";

    std::size_t count = 0;

    for (auto const& n : validators.values ())
    {
        JLOG (j_.trace()) <<
            "Processing '" << n << "'";

        boost::smatch match;

        if (!boost::regex_match (n, match, re))
        {
            JLOG (j_.error()) <<
                "Malformed entry: '" << n << "'";
            return false;
        }

        auto const id = parseBase58<PublicKey>(
            TokenType::TOKEN_NODE_PUBLIC, match[1]);

        if (!id)
        {
            JLOG (j_.error()) <<
                "Invalid node identity: " << match[1];
            return false;
        }

        if (trusted (*id))
        {
            JLOG (j_.warn()) <<
                "Duplicate node identity: " << match[1];
            continue;
        }

        if (insertPermanentKey(*id, trim_whitespace (match[2])))
            ++count;
    }

    JLOG (j_.debug()) <<
        "Loaded " << count << " entries";

    return true;
}

}
