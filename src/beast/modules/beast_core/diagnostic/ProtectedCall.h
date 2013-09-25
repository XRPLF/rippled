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

#ifndef BEAST_CORE_PROTECTEDCALL_H_INCLUDED
#define BEAST_CORE_PROTECTEDCALL_H_INCLUDED

/** Call a function in a protected exception context.

    This is for intercepting unhandled exceptions, reporting on the extended
    information provided by @ref Throw, and calling a customizable unhandled
    exception callback. Some implementations will also catch native exceptions
    such as memory violations or segmentation faults.

    An unhandled exception should terminate the process with a non zero
    return code.

    To use this, construct an instance with your funtion and arguments as
    parameters. For example:

    @code

    extern void funcThatMightThrow (int numberOfTimes);

    ProtectedCall (&funcThatMightThrow, 3);

    @endcode

    Every Beast Thread object's @ref Thread::run method is wrapped in a
    @ProtectedCall

    @see Thread, Throw
*/
class ProtectedCall
{
public:
    struct Exception
    {
    };

    /** This receives the unhandled exception. */
    struct Handler
    {
        /** Called when an uhandled exception is thrown.

            @note This can be called from multiple threads, which is
                  why it is const.
        */
        virtual void onException (Exception const& e) const = 0;
    };

    /** The default handler writes to std::cerr makes the process exit. */
    class DefaultHandler;

    static void setHandler (Handler const& handler);

public:
#if BEAST_VARIADIC_MAX >= 1
    template <class Fn>
    explicit ProtectedCall (Fn f)
    { callf (functional::bind (f)); }
#endif

#if BEAST_VARIADIC_MAX >= 2
    template <class Fn, class T1>
    ProtectedCall (Fn f, T1 t1)
    { callf  (functional::bind (f, t1)); }
#endif

#if BEAST_VARIADIC_MAX >= 3
    template <class Fn, class T1, class T2>
    ProtectedCall (Fn f, T1 t1, T2 t2)
    { callf  (functional::bind (f, t1, t2)); }
#endif

#if BEAST_VARIADIC_MAX >= 4
    template <class Fn, class T1, class T2, class T3>
    ProtectedCall (Fn f, T1 t1, T2 t2, T3 t3)
    { callf  (functional::bind (f, t1, t2, t3)); }
#endif

#if BEAST_VARIADIC_MAX >= 5
    template <class Fn, class T1, class T2, class T3, class T4>
    ProtectedCall (Fn f, T1 t1, T2 t2, T3 t3, T4 t4)
    { callf  (functional::bind (f, t1, t2, t3, t4)); }
#endif

#if BEAST_VARIADIC_MAX >= 6
    template <class Fn, class T1, class T2, class T3, class T4, class T5>
    ProtectedCall (Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
    { callf  (functional::bind (f, t1, t2, t3, t4, t5)); }
#endif

#if BEAST_VARIADIC_MAX >= 7
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6>
    ProtectedCall (Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
    { callf  (functional::bind (f, t1, t2, t3, t4, t5, t6)); }
#endif

#if BEAST_VARIADIC_MAX >= 8
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    ProtectedCall (Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7)
    { callf  (functional::bind (f, t1, t2, t3, t4, t5, t6, t7)); }
#endif

#if BEAST_VARIADIC_MAX >= 9
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    ProtectedCall (Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
    { callf  (functional::bind (f, t1, t2, t3, t4, t5, t6, t7, t8)); }
#endif

private:
    typedef SharedFunction <void (void)> FunctionType;
    typedef FunctionType::Call Call;

    template <class Function>
    void callf (Function f)
    {
        //CallType <Functor> wrapper (f);
        FunctionType::CallType <Function> wrapper (
            BEAST_MOVE_CAST(Function)(f));

        call (wrapper);
    }

    void call (Call& call);

private:
    static Handler const* s_handler;
};

#endif
