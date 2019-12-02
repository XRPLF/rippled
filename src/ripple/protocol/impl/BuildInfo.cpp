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

#include <ripple/basics/contract.h>
#include <ripple/beast/core/SemanticVersion.h>
#include <ripple/protocol/BuildInfo.h>

#include <boost/preprocessor/stringize.hpp>

namespace ripple {

namespace BuildInfo {

//--------------------------------------------------------------------------
//  The build version number. You must edit this for each release
//  and follow the format described at http://semver.org/
//------------------------------------------------------------------------------
char const* const versionString = "1.5.0-b1"

#if defined(DEBUG) || defined(SANITIZER)
        "+"
#ifdef DEBUG
        "DEBUG"
#ifdef SANITIZER
        "."
#endif
#endif

#ifdef SANITIZER
        BOOST_PP_STRINGIZE(SANITIZER)
#endif
#endif

    //--------------------------------------------------------------------------
    ;

//
// Don't touch anything below this line
//

std::string const&
getVersionString ()
{
    static std::string const value = [] {
        std::string const s = versionString;
        beast::SemanticVersion v;
        if (!v.parse (s) || v.print () != s)
            LogicError (s + ": Bad server version string");
        return s;
    }();
    return value;
}

std::string const& getFullVersionString ()
{
    static std::string const value =
        "rippled-" + getVersionString();
    return value;
}

} // BuildInfo

} // ripple
