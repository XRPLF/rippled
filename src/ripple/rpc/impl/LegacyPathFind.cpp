//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2014 Ripple Labs Inc.

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

#include <ripple/app/main/Application.h>
#include <ripple/app/misc/LoadFeeTrack.h>
#include <ripple/core/Job.h>
#include <ripple/core/JobQueue.h>
#include <ripple/rpc/impl/LegacyPathFind.h>
#include <ripple/rpc/impl/Tuning.h>

namespace ripple {
namespace RPC {

LegacyPathFind::LegacyPathFind (bool isAdmin, Application& app) : m_isOk (false)
{
    if (isAdmin)
    {
        ++inProgress;
        m_isOk = true;
        return;
    }

    auto const& jobCount = app.getJobQueue ().getJobCountGE (jtCLIENT);
    if (jobCount > Tuning::maxPathfindJobCount || app.getFeeTrack().isLoadedLocal ())
        return;

    while (true)
    {
        int prevVal = inProgress.load();
        if (prevVal >= Tuning::maxPathfindsInProgress)
            return;

        if (inProgress.compare_exchange_strong (
                prevVal,
                prevVal + 1,
                std::memory_order_release,
                std::memory_order_relaxed))
        {
            m_isOk = true;
            return;
        }
    }
}

LegacyPathFind::~LegacyPathFind ()
{
    if (m_isOk)
        --inProgress;
}

std::atomic <int> LegacyPathFind::inProgress (0);

} // RPC
} // ripple
