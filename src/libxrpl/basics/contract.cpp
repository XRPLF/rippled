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

#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <cstdlib>
#include <iostream>
#include <string>

namespace ripple {

void
LogThrow(std::string const& title)
{
    JLOG(debugLog().warn()) << title;
}

[[noreturn]] void
LogicError(std::string const& s) noexcept
{
    JLOG(debugLog().fatal()) << s;
    std::cerr << "Logic error: " << s << std::endl;
    // Use a non-standard contract naming here (without namespace) because
    // it's the only location where various unrelated execution paths may
    // register an error; this is also why the "message" parameter is passed
    // here.
    // For the above reasons, we want this contract to stand out.
    UNREACHABLE("LogicError", {{"message", s}});
    std::abort();
}

}  // namespace ripple
