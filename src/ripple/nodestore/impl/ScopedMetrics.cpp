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

#include <ripple/nodestore/ScopedMetrics.h>
#include <boost/thread/tss.hpp>

namespace ripple {
namespace NodeStore {

static
void
cleanup (ScopedMetrics*)
{
}

static
boost::thread_specific_ptr<ScopedMetrics> scopedMetricsPtr (&cleanup);

ScopedMetrics::ScopedMetrics () : prev_ (scopedMetricsPtr.get ())
{
    scopedMetricsPtr.reset (this);
}

ScopedMetrics::~ScopedMetrics ()
{
    scopedMetricsPtr.reset (prev_);
}

ScopedMetrics*
ScopedMetrics::get ()
{
    return scopedMetricsPtr.get ();
}

void
ScopedMetrics::incrementThreadFetches ()
{
    if (scopedMetricsPtr.get ())
        ++scopedMetricsPtr.get ()->fetches;
}

}
}
