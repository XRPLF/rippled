//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2014 Ripple Labs Inc.

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

#ifndef RIPPLE_LEGACY_CORE_TYPES_H_INCLUDED
#define RIPPLE_LEGACY_CORE_TYPES_H_INCLUDED

#include <ripple/app/ledger/LedgerEntrySet.h>
#include <ripple/protocol/Book.h>

#include <chrono>
#include <cstdint>

namespace ripple {
namespace legacy {
namespace core {

/** A mutable view that overlays an immutable ledger to track changes. */
typedef LedgerEntrySet LedgerView;

/** A clock representing network time.
    This measures seconds since the Ripple epoch as seen
    by the ledger close clock.
*/
class Clock // : public abstract_clock <std::chrono::seconds>
{
public:
    typedef std::uint32_t time_point;
    typedef std::chrono::seconds duration;
};

} // core
} // legacy
} // ripple

#endif
