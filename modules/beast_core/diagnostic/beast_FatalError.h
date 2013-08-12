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

    /** Set the fatal error reporter.

        Note that if a fatal error is raised during the construction of
        objects with static storage duration, it might not be possible to
        set the reporter before the error is raised. The solution is not
        to use objects with static storage duration that have non-trivial
        constructors, use SharedSingleton instead.

        The default behavior when no reporter is set is to invoke
        the base class version of Reporter::onFatalError.

        If a reporter was previously set, this routine will do nothing.

        @see SharedSingleton, Reporter
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
        @param filePath Pass __FILE__ here.
        @param lineNumber Pass __LINE__ here.
    */
    FatalError (char const* message, char const* filePath, int lineNumber);

private:
    static Static::Storage <Atomic <Reporter*>, FatalError> s_reporter;
};

//------------------------------------------------------------------------------

#if defined (fatal_error) || \
    defined (fatal_condition) || \
    defined (fatal_assert) || \
    defined (meets_condition) || \
    defined (meets_precondition) || \
    defined (meets_postcondition) || \
    defined (meets_invariant) || \
    defined (check_precondition) || \
    defined (check_postcondition) || \
    defined (check_invariant)
#error "Programming by contract macros cannot be overriden!"
#endif

//------------------------------------------------------------------------------

/** Report a fatal error message and terminate the application.
    Normally you won't call this directly.
*/
inline void reportFatalError (char const* message, char const* fileName, int lineNumber)
{
    if (beast::beast_isRunningUnderDebugger())
        beast_breakDebugger;
    FatalError (message, fileName, lineNumber);
    BEAST_ANALYZER_NORETURN
}

/** Report a fatal error message and terminate the application.
    This macro automatically fills in the file and line number
    Meets this declaration syntax:
    @code inline void fatal_error (char const* message); @endif
    @see FatalError
*/
#define fatal_error(message) reportFatalError (message, __FILE__, __LINE__)

/** Reports a fatal error message type if the condition is false
    The condition is always evaluated regardless of settings.
    Meets this declaration syntax:
    @code inline void fatal_condition (bool condition, char const* category); @endcode
*/
#define fatal_condition(condition,category) static_cast <void> \
    (((!!(condition)) || (reportFatalError ( \
        category " '" BEAST_STRINGIFY(condition) "' failed.", __FILE__, __LINE__), 0)))

/** Replacement for assert which generates a fatal error if the condition is false.
    The condition is always evaluated regardless of compilation settings.
    Meets this declaration syntax:
    @code inline void fatal_assert (bool condition); @endcode
*/
#define fatal_assert(condition) fatal_condition(condition,"Assertion")

/** Reports a fatal error message type if the condition is false
    The condition is always evaluated regardless of settings.
    Meets this declaration syntax:
    @code inline void fatal_condition (bool condition, char const* category); @endcode
*/
#define meets_condition(condition,category) static_cast <bool> \
    (((!!(condition)) || (reportFatalError ( \
        category " '" BEAST_STRINGIFY(condition) "' failed.", __FILE__, __LINE__), false)))

/** Condition tests for programming by contract.
    The condition is always evaluated regardless of settings, and gets returned.
    Meets this declaration syntax:
    @code inline bool meets_condition (bool); @endcode
*/
/** @{ */
#define meets_precondition(condition)  meets_condition(condition,"Pre-condition")
#define meets_postcondition(condition) meets_condition(condition,"Post-condition")
#define meets_invariant(condition)     meets_condition(condition,"Invariant")
/** @} */

/** Condition tests for programming by contract.
    The condition is evaluated only if BEAST_DISABLE_CONTRACT_CHECKS is 0.
    Meets this declaration syntax:
    @code inline void check_condition (bool); @endcode
    @see BEAST_DISABLE_CONTRACT_CHECKS
*/
/** @{ */
#if ! BEAST_DISABLE_CONTRACT_CHECKS
# define check_precondition(condition)  meets_precondition(condition)
# define check_postcondition(condition) meets_postcondition(condition)
# define check_invariant(condition)     meets_invariant(condition)
#else
# define check_precondition(condition)  ((void)0)
# define check_postcondition(condition) ((void)0)
# define check_invariant(condition)     ((void)0)
#endif
/** @} */

#endif
