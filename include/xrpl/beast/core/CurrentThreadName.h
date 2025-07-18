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

#ifndef BEAST_CORE_CURRENT_THREAD_NAME_H_INCLUDED
#define BEAST_CORE_CURRENT_THREAD_NAME_H_INCLUDED

#include <string>
#include <string_view>

namespace beast {

/** Changes the name of the caller thread.
    Different OSes may place different length or content limits on this name.
*/
void
setCurrentThreadName(std::string_view newThreadName);

/** Returns the name of the caller thread.

    The name returned is the name as set by a call to setCurrentThreadName().
    If the thread name is set by an external force, then that name change
    will not be reported.

    If no name has ever been set, then the empty string is returned.
*/
std::string
getCurrentThreadName();

}  // namespace beast

#endif
