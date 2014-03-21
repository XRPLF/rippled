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

#ifndef RIPPLE_RIPPLESYSTEM_H
#define RIPPLE_RIPPLESYSTEM_H

namespace ripple {

/** Protocol specific constant globals. */
// VFALCO NOTE use these from now on instead of the macros!!
class RippleSystem
{
public:
    static inline char const* getSystemName ()
    {
        return "ripple";
    }

    static char const* getCurrencyCode ()
    {
        return "XRP";
    }

    static char const* getCurrencyCodeRipple ()
    {
        return "XRR";
    }

    static int getCurrencyPrecision ()
    {
        return 6;
    }
};

// VFALCO TODO I would love to replace these macros with the language
//         constructs above. The problem is the way they are used at
//         the point of call, i.e. "User-agent:" SYSTEM_NAME
//         It will be necessary to rewrite some of them to use string streams.
//
#define SYSTEM_NAME                 "ripple"
#define SYSTEM_CURRENCY_CODE        "XRP"
#define SYSTEM_CURRENCY_PRECISION   6
#define SYSTEM_CURRENCY_CODE_RIPPLE "XRR"

} // ripple

#endif
