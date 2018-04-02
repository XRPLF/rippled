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

#include <ripple/basics/UptimeTimer.h>

#include <atomic>

namespace ripple {

UptimeTimer::UptimeTimer ()
    : m_elapsedTime (0)
    , m_startTime (::time (0))
    , m_isUpdatingManually (false)
{
}

UptimeTimer::~UptimeTimer ()
{
}

int UptimeTimer::getElapsedSeconds () const
{
    int result;

    if (m_isUpdatingManually)
    {
        std::atomic_thread_fence (std::memory_order_seq_cst);
        result = m_elapsedTime;
    }
    else
    {
        // VFALCO TODO use time_t instead of int return
        result = static_cast <int> (::time (0) - m_startTime);
    }

    return result;
}

void UptimeTimer::beginManualUpdates ()
{
    //assert (!m_isUpdatingManually);

    m_isUpdatingManually = true;
}

void UptimeTimer::endManualUpdates ()
{
    //assert (m_isUpdatingManually);

    m_isUpdatingManually = false;
}

void UptimeTimer::incrementElapsedTime ()
{
    //assert (m_isUpdatingManually);
    ++m_elapsedTime;
}

UptimeTimer& UptimeTimer::getInstance ()
{
    static UptimeTimer instance;

    return instance;
}

} // ripple
