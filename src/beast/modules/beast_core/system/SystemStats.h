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

//==============================================================================
/**
    Contains methods for finding out about the current hardware and OS configuration.
*/
class BEAST_API SystemStats : public Uncopyable
{
public:
    //==============================================================================
    /** Returns the current version of BEAST,
        See also the BEAST_VERSION, BEAST_MAJOR_VERSION and BEAST_MINOR_VERSION macros.
    */
    static String getBeastVersion();

    //==============================================================================
    /** The set of possible results of the getOperatingSystemType() method. */
    enum OperatingSystemType
    {
        UnknownOS   = 0,

        MacOSX_10_4 = 0x1004,
        MacOSX_10_5 = 0x1005,
        MacOSX_10_6 = 0x1006,
        MacOSX_10_7 = 0x1007,
        MacOSX_10_8 = 0x1008,

        Linux       = 0x2000,
        FreeBSD     = 0x2001,
        Android     = 0x3000,

        Win2000     = 0x4105,
        WinXP       = 0x4106,
        WinVista    = 0x4107,
        Windows7    = 0x4108,
        Windows8    = 0x4109,

        Windows     = 0x4000,   /**< To test whether any version of Windows is running,
                                     you can use the expression ((getOperatingSystemType() & Windows) != 0). */

        iOS         = 0x8000
    };

    /** Returns the type of operating system we're running on.

        @returns one of the values from the OperatingSystemType enum.
        @see getOperatingSystemName
    */
    static OperatingSystemType getOperatingSystemType();

    /** Returns the name of the type of operating system we're running on.

        @returns a string describing the OS type.
        @see getOperatingSystemType
    */
    static String getOperatingSystemName();

    /** Returns true if the OS is 64-bit, or false for a 32-bit OS.
    */
    static bool isOperatingSystem64Bit();

    /** Returns an environment variable.
        If the named value isn't set, this will return the defaultValue string instead.
    */
    static String getEnvironmentVariable (const String& name, const String& defaultValue);

    //==============================================================================
    /** Returns the current user's name, if available.
        @see getFullUserName()
    */
    static String getLogonName();

    /** Returns the current user's full name, if available.
        On some OSes, this may just return the same value as getLogonName().
        @see getLogonName()
    */
    static String getFullUserName();

    /** Returns the host-name of the computer. */
    static String getComputerName();

    /** Returns the language of the user's locale.
        The return value is a 2 or 3 letter language code (ISO 639-1 or ISO 639-2)
    */
    static String getUserLanguage();

    /** Returns the region of the user's locale.
        The return value is a 2 letter country code (ISO 3166-1 alpha-2).
    */
    static String getUserRegion();

    /** Returns the user's display language.
        The return value is a 2 or 3 letter language code (ISO 639-1 or ISO 639-2)
    */
    static String getDisplayLanguage();

    //==============================================================================
    // CPU and memory information..

    /** Returns the number of CPU cores. */
    static int getNumCpus() noexcept;

    /** Returns the approximate CPU speed.
        @returns    the speed in megahertz, e.g. 1500, 2500, 32000 (depending on
                    what year you're reading this...)
    */
    static int getCpuSpeedInMegaherz();

    /** Returns a string to indicate the CPU vendor.
        Might not be known on some systems.
    */
    static String getCpuVendor();

    static bool hasMMX() noexcept; /**< Returns true if Intel MMX instructions are available. */
    static bool hasSSE() noexcept; /**< Returns true if Intel SSE instructions are available. */
    static bool hasSSE2() noexcept; /**< Returns true if Intel SSE2 instructions are available. */
    static bool hasSSE3() noexcept; /**< Returns true if Intel SSE2 instructions are available. */
    static bool has3DNow() noexcept; /**< Returns true if AMD 3DNOW instructions are available. */

    //==============================================================================
    /** Finds out how much RAM is in the machine.
        @returns    the approximate number of megabytes of memory, or zero if
                    something goes wrong when finding out.
    */
    static int getMemorySizeInMegabytes();

    /** Returns the system page-size.
        This is only used by programmers with beards.
    */
    static int getPageSize();

    //==============================================================================
    /** Returns a backtrace of the current call-stack.
        The usefulness of the result will depend on the level of debug symbols
        that are available in the executable.
    */
    static String getStackBacktrace();

    /** A void() function type, used by setApplicationCrashHandler(). */
    typedef void (*CrashHandlerFunction)();

    /** Sets up a global callback function that will be called if the application
        executes some kind of illegal instruction.

        You may want to call getStackBacktrace() in your handler function, to find out
        where the problem happened and log it, etc.
    */
    static void setApplicationCrashHandler (CrashHandlerFunction);

private:
    //==============================================================================

    SystemStats();
};


#endif   // BEAST_SYSTEMSTATS_H_INCLUDED
