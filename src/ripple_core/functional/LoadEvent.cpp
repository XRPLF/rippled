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


LoadEvent::LoadEvent (LoadMonitor& monitor, const std::string& name, bool shouldStart)
    : mMonitor (monitor)
    , mRunning (false)
    , mName (name)
{
    mStartTime = boost::posix_time::microsec_clock::universal_time ();

    if (shouldStart)
        start ();
}

LoadEvent::~LoadEvent ()
{
    if (mRunning)
        stop ();
}

void LoadEvent::reName (const std::string& name)
{
    mName = name;
}

void LoadEvent::start ()
{
    mRunning = true;
    mStartTime = boost::posix_time::microsec_clock::universal_time ();
}

void LoadEvent::stop ()
{
    assert (mRunning);

    mRunning = false;
    mMonitor.addCountAndLatency (mName,
                                 static_cast<int> ((boost::posix_time::microsec_clock::universal_time () - mStartTime).total_milliseconds ()));
}
