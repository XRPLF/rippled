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

#ifndef RIPPLE_LOADEVENT_H
#define RIPPLE_LOADEVENT_H

#include <beast/chrono/RelativeTime.h>

namespace ripple {

class LoadMonitor;

// VFALCO NOTE What is the difference between a LoadEvent and a LoadMonitor?
// VFALCO TODO Rename LoadEvent to ScopedLoadSample
//
//        This looks like a scoped elapsed time measuring class
//
class LoadEvent
{
public:
    // VFALCO NOTE Why are these shared pointers? Wouldn't there be a
    //             piece of lifetime-managed calling code that can simply own
    //             the object?
    //
    //             Why both kinds of containers?
    //
    typedef std::shared_ptr <LoadEvent> pointer;
    typedef std::unique_ptr <LoadEvent>            autoptr;

public:
    // VFALCO TODO remove the dependency on LoadMonitor. Is that possible?
    LoadEvent (LoadMonitor& monitor,
               const std::string& name,
               bool shouldStart);

    ~LoadEvent ();

    std::string const& name () const;
    double getSecondsWaiting() const;
    double getSecondsRunning() const;
    double getSecondsTotal() const;

    // VFALCO TODO rename this to setName () or setLabel ()
    void reName (const std::string& name);

    // Start the measurement. The constructor calls this automatically if
    // shouldStart is true. If the operation is aborted, start() can be
    // called again later.
    //
    void start ();

    // Stops the measurement and reports the results. The time reported is
    // measured from the last call to start.
    //
    void stop ();

private:
    LoadMonitor& m_loadMonitor;
    bool m_isRunning;
    std::string m_name;
    // VFALCO TODO Replace these with chrono
    beast::RelativeTime m_timeStopped;
    beast::RelativeTime m_timeStarted;
    double m_secondsWaiting;
    double m_secondsRunning;
};

} // ripple

#endif
