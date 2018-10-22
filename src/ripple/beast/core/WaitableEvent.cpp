//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

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

#include <ripple/beast/core/WaitableEvent.h>

namespace beast {

WaitableEvent::WaitableEvent (const bool useManualReset, bool initiallySignaled)
    : triggered (false), manualReset (useManualReset)
{
    if (initiallySignaled)
        signal ();
}

bool WaitableEvent::wait () const
{
    std::unique_lock<std::mutex> lk{mutex};
    while (!triggered)
        condition.wait(lk);
    if (!manualReset)
        triggered = false;
    return true;
}

bool WaitableEvent::wait (std::chrono::milliseconds timeOut) const
{
    std::unique_lock<std::mutex> lk{mutex};
    while (!triggered)
    {
        auto status = condition.wait_for(lk, timeOut);
        if (status == std::cv_status::timeout)
            return false;
    }
    if (!manualReset)
        triggered = false;
    return true;
}

void WaitableEvent::signal() const
{
    std::lock_guard<std::mutex> lk{mutex};
    triggered = true;
    condition.notify_all();
}

void WaitableEvent::reset() const
{
    std::lock_guard<std::mutex> lk{mutex};
    triggered = false;
}

}
