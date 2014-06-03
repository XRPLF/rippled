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

#ifndef BEAST_PROCESS_H_INCLUDED
#define BEAST_PROCESS_H_INCLUDED

namespace beast {

//==============================================================================
/** Represents the current executable's process.

    This contains methods for controlling the current application at the
    process-level.

    @see Thread, BEASTApplication
*/
class Process : public Uncopyable
{
public:
    //==============================================================================
    enum ProcessPriority
    {
        LowPriority         = 0,
        NormalPriority      = 1,
        HighPriority        = 2,
        RealtimePriority    = 3
    };

    /** Changes the current process's priority.

        @param priority     the process priority, where
                            0=low, 1=normal, 2=high, 3=realtime
    */
    static void setPriority (const ProcessPriority priority);

    /** Kills the current process immediately.

        This is an emergency process terminator that kills the application
        immediately - it's intended only for use only when something goes
        horribly wrong.

        @see BEASTApplication::quit
    */
    static void terminate();

    //==============================================================================
    /** Returns true if this application process is the one that the user is
        currently using.
    */
    static bool isForegroundProcess();

    /** Attempts to make the current process the active one.
        (This is not possible on some platforms).
    */
    static void makeForegroundProcess();

    //==============================================================================
    /** Raises the current process's privilege level.

        Does nothing if this isn't supported by the current OS, or if process
        privilege level is fixed.
    */
    static void raisePrivilege();

    /** Lowers the current process's privilege level.

        Does nothing if this isn't supported by the current OS, or if process
        privilege level is fixed.
    */
    static void lowerPrivilege();

    /** Returns true if this process is being hosted by a debugger. */
    static bool isRunningUnderDebugger();

    //==============================================================================
    /** Tries to launch the OS's default reader application for a given file or URL. */
    static bool openDocument (const String& documentURL, const String& parameters);

   #if BEAST_WINDOWS || DOXYGEN
    //==============================================================================
    /** WINDOWS ONLY - This returns the HINSTANCE of the current module.

        The return type is a void* to avoid being dependent on windows.h - just cast
        it to a HINSTANCE to use it.

        In a normal BEAST application, this will be automatically set to the module
        handle of the executable.

        If you've built a DLL and plan to use any BEAST messaging or windowing classes,
        you'll need to make sure you call the setCurrentModuleInstanceHandle()
        to provide the correct module handle in your DllMain() function, because
        the system relies on the correct instance handle when opening windows.
    */
    static void* getCurrentModuleInstanceHandle() noexcept;

    /** WINDOWS ONLY - Sets a new module handle to be used by the library.

        The parameter type is a void* to avoid being dependent on windows.h, but it actually
        expects a HINSTANCE value.

        @see getCurrentModuleInstanceHandle()
    */
    static void setCurrentModuleInstanceHandle (void* newHandle) noexcept;
   #endif

   #if BEAST_MAC || DOXYGEN
    //==============================================================================
    /** OSX ONLY - Shows or hides the OSX dock icon for this app. */
    static void setDockIconVisible (bool isVisible);
   #endif

private:
    Process();
};

} // beast

#endif   // BEAST_PROCESS_H_INCLUDED
