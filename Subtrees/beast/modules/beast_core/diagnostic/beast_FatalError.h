//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_FATALERROR_H_INCLUDED
#define BEAST_FATALERROR_H_INCLUDED

/** Signal a fatal error.

    A fatal error indicates that the program has encountered an unexpected
    situation and cannot continue safely. Reasons for raising a fatal error
    would be to protect data integrity, prevent valuable resources from being
    wasted, or to ensure that the user does not experience undefined behavior.

    This function will end the process with exit code EXIT_FAILURE. Before
    the process is terminated, a listener object gets notified so that the
    client application can perform logging or emit further diagnostics.
*/
class FatalError : Uncopyable
{
public:
    struct Reporter
    {
        /** Called when a fatal error is raised.

            Because the program is likely in an inconsistent state, it is a
            good idea to do as little as possible from within this function.
            It will be called from the thread that raised the fatal error.

            @param message The error message.
            @param stackBackTrace The stack of the thread that raised the error.
            @param fileName The source file that raised the error.
            @param lineNumber The line number in the source file.
        */
        virtual void onFatalError (char const* message,
                                   char const* stackBacktrace,
                                   char const* fileName,
                                   int lineNumber) = 0;
    };

    /** Set the fatal error reporter.

        Note that if a fatal error is raised during the construction of
        objects with static storage duration, it might not be possible to
        set the reporter before the error is raised.

        The solution is not to use objects with static storage duration
        that have non-trivial constructors, use SharedSingleton instead.

        If a reporter was previously set, this routine will do nothing.

        @see SharedSingleton
    */
    static void setReporter (Reporter& reporter);

    /** Clear the fatal error reporter.

        If the current reporter is the same as the one passed in, this
        will remove the reporter.
    */
    static void resetReporter (Reporter& reporter);

    /** Raise a fatal error.

        If multiple threads raise an error, only one will succeed. The
        other threads will be blocked before the process terminates.

        @param message A null terminated string, which should come from a constant.
        @param fileName Pass __FILE__ here.
        @param lineNumber Pass __LINE__ here.
    */
    FatalError (char const* message, char const* fileName, int lineNumber);

private:
    typedef CriticalSection LockType;

    static Static::Storage <Atomic <Reporter*>, FatalError> s_reporter;

    LockType m_mutex;
};

#endif
