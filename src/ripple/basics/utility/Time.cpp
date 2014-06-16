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

namespace ripple {

// VFALCO TODO Tidy this up into a RippleTime object

//
// Time support
// We have our own epoch.
//

boost::posix_time::ptime ptEpoch ()
{
    return boost::posix_time::ptime (boost::gregorian::date (2000, boost::gregorian::Jan, 1));
}

int iToSeconds (boost::posix_time::ptime ptWhen)
{
    return ptWhen.is_not_a_date_time ()
           ? -1
           : (ptWhen - ptEpoch ()).total_seconds ();
}

// Convert our time in seconds to a ptime.
boost::posix_time::ptime ptFromSeconds (int iSeconds)
{
    return iSeconds < 0
           ? boost::posix_time::ptime (boost::posix_time::not_a_date_time)
           : ptEpoch () + boost::posix_time::seconds (iSeconds);
}

// Convert from our time to UNIX time in seconds.
uint64_t utFromSeconds (int iSeconds)
{
    boost::posix_time::time_duration    tdDelta =
        boost::posix_time::ptime (boost::gregorian::date (2000, boost::gregorian::Jan, 1))
        - boost::posix_time::ptime (boost::gregorian::date (1970, boost::gregorian::Jan, 1))
        + boost::posix_time::seconds (iSeconds)
        ;

    return tdDelta.total_seconds ();
}

} // ripple
