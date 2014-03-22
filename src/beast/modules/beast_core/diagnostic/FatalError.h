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

#ifndef BEAST_CORE_FATALERROR_H_INCLUDED
#define BEAST_CORE_FATALERROR_H_INCLUDED

namespace beast
{

/** Signal a fatal error.

    A fatal error indicates that the program has encountered an unexpected
    situation and cannot continue safely. Reasons for raising a fatal error
    would be to protect data integrity, prevent valuable resources from being
    wasted, or to ensure that the user does not experience undefined behavior.

    This function will end the process with exit code EXIT_FAILURE. Before
    the process is terminated, a listener object gets notified so that the
    client application can perform logging or emit further diagnostics.
*/
class FatalError : public Uncopyable
{
public:
    struct Reporter
    {
        virtual ~Reporter () { }

        /** Called when a fatal error is raised.

            Because the program is likely in an inconsistent state, it is a
            good idea to do as little as possible from within this function.
            It will be called from the thread that raised the fatal error.

            The default implementation of this function first calls
            formatMessage to produce the string, then calls reportMessage
            to report the results.

            You can override this to perform custom formatting.

            @note filePath may be a zero length string if identifying
                  information was stripped from the executable for security.

            @note stackBacktrace will be a string with zero characters for
                  platforms for which which don't support stack crawls, or
                  when symbolic information is missing from the executable.

            @param message The error message.
            @param stackBackTrace The stack of the thread that raised the error.
            @param filePath A full or partial path to the source file that raised the error.
            @param lineNumber The line number in the source file.
        */
        virtual void onFatalError (char const* message,
                                   char const* stackBacktrace,
                                   char const* filePath,
                                   int lineNumber);

        /** Called to report the message.

            The default implementation simply writes this to standard error.
            You can override this to perform additional things like logging
            to a file or sending the message to another process.

            @param formattedMessage The message to report.
        */
        virtual void reportMessage (String& formattedMessage);

    protected:
       /** Called to format the message.

            The default implementation calls formatFilePath to produce
            a formatted file name, and then creates a suitable string
            containing all of the information.

            You can override this function to format your own messages.

            @param message The message from the report.
            @param stackBacktrace The stack backtrace from the report.
            @param filePath The file path from the report.
            @param lineNumber The line number from the report
        */
        virtual String formatMessage (char const* message,
                                      char const* stackBacktrace,
                                      char const* filePath,
                                      int lineNumber);

        /** Call to reformat the file path.

            Usually the file is a full path, which we really don't care
            to see and can also be a security hole.

            The default implementation removes most of the useless
            directory components from the front.

            You can override this to do a custom format on the file path.
        */
        virtual String formatFilePath (char const* filePath);
    };

    /** Returns the current fatal error reporter. */
    static Reporter* getReporter ();

    /** Set the fatal error reporter.

        Note that if a fatal error is raised during the construction of
        objects with static storage duration, it might not be possible to
        set the reporter before the error is raised. The solution is not
        to use objects with static storage duration that have non-trivial
        constructors, use SharedSingleton instead.

        The default behavior when no reporter is set is to invoke
        the base class version of Reporter::onFatalError.

        If a reporter was previously set, this routine will do nothing.

        @return The previous Reporter (Which may be null).

        @see SharedSingleton, Reporter
    */
    static Reporter* setReporter (Reporter* reporter);

    /** Raise a fatal error.

        If multiple threads raise an error, only one will succeed. The
        other threads will be blocked before the process terminates.

        @param message A null terminated string, which should come from a constant.
        @param filePath Pass __FILE__ here.
        @param lineNumber Pass __LINE__ here.
    */
    FatalError (char const* message, char const* filePath, int lineNumber);

private:
    static Reporter* s_reporter;
};

} // beast

#endif
