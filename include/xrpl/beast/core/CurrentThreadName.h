// Portions of this file are from JUCE (http://www.juce.com).
// Copyright (c) 2013 - Raw Material Software Ltd.
// Please visit http://www.juce.com

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
