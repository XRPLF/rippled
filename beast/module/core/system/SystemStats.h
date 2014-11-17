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

#ifndef BEAST_SYSTEMSTATS_H_INCLUDED
#define BEAST_SYSTEMSTATS_H_INCLUDED

namespace beast
{

//==============================================================================
/**
    Contains methods for finding out about the current hardware and OS configuration.
*/
namespace SystemStats
{
    //==============================================================================
    /** Returns the current version of BEAST,
        See also the BEAST_VERSION, BEAST_MAJOR_VERSION and BEAST_MINOR_VERSION macros.
    */
    std::string getBeastVersion();

    //==============================================================================
    /** Returns the host-name of the computer. */
    std::string getComputerName();

    //==============================================================================
    /** Returns a backtrace of the current call-stack.
        The usefulness of the result will depend on the level of debug symbols
        that are available in the executable.
    */
    std::vector <std::string>
    getStackBacktrace();

    /** A void() function type, used by setApplicationCrashHandler(). */
    typedef void (*CrashHandlerFunction)();

    /** Sets up a global callback function that will be called if the application
        executes some kind of illegal instruction.

        You may want to call getStackBacktrace() in your handler function, to find out
        where the problem happened and log it, etc.
    */
    void setApplicationCrashHandler (CrashHandlerFunction);
};

} // beast

#endif   // BEAST_SYSTEMSTATS_H_INCLUDED
