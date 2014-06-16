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

LoadEvent::LoadEvent (LoadMonitor& monitor, const std::string& name, bool shouldStart)
    : m_loadMonitor (monitor)
    , m_isRunning (false)
    , m_name (name)
    , m_timeStopped (beast::RelativeTime::fromStartup())
    , m_secondsWaiting (0)
    , m_secondsRunning (0)
{
    if (shouldStart)
        start ();
}

LoadEvent::~LoadEvent ()
{
    if (m_isRunning)
        stop ();
}

std::string const& LoadEvent::name () const
{
    return m_name;
}

double LoadEvent::getSecondsWaiting() const
{
    return m_secondsWaiting;
}

double LoadEvent::getSecondsRunning() const
{
    return m_secondsRunning;
}

double LoadEvent::getSecondsTotal() const
{
    return m_secondsWaiting + m_secondsRunning;
}

void LoadEvent::reName (const std::string& name)
{
    m_name = name;
}

void LoadEvent::start ()
{
    beast::RelativeTime const currentTime (beast::RelativeTime::fromStartup());

    // If we already called start, this call will replace the previous one.
    if (m_isRunning)
    {
        m_secondsWaiting += (currentTime - m_timeStarted).inSeconds();
    }
    else
    {
        m_secondsWaiting += (currentTime - m_timeStopped).inSeconds();
        m_isRunning = true;
    }

    m_timeStarted = currentTime;
}

void LoadEvent::stop ()
{
    bassert (m_isRunning);

    m_timeStopped = beast::RelativeTime::fromStartup();
    m_secondsRunning += (m_timeStopped - m_timeStarted).inSeconds();

    m_isRunning = false;
    m_loadMonitor.addLoadSample (*this);
}

} // ripple
