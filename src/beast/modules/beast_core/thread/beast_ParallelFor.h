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

#ifndef BEAST_PARALLELFOR_H_INCLUDED
#define BEAST_PARALLELFOR_H_INCLUDED

/*============================================================================*/
/**
  Parallel for loop.

  This uses a ThreadGroup to iterate through a for loop in parallel. The
  following two pieces of code perform identical operations:

  @code

  extern void function (int loopIndex);

  // Serial computation
  //
  for (int i = 0; i < numberOfIterations; ++i)
    function (i);

  // Parallel computation
  //
  ParallelFor().loop (numberOfIterations, &function);

  @endcode

  `function` is a caller provided functor. Convenience functions are provided
  for automatic binding to member or non member functions with up to 8
  arguments (not including the loop index).

  @note The last argument to function () is always the loop index.

  @see ThreadGroup

  @ingroup beast_concurrent
*/
class BEAST_API ParallelFor : public Uncopyable
{
public:
    /** Create a parallel for loop.

        It is best to keep this object around instead of creating and destroying
        it every time you need to run a loop.

        @param pool The ThreadGroup to use. If this is omitted then a singleton
                    ThreadGroup is used which contains one thread per CPU.
    */
    explicit ParallelFor (ThreadGroup& pool = *GlobalThreadGroup::getInstance ());

    /** Determine the number of threads in the group.

        @return The number of threads in the group.
    */
    int getNumberOfThreads () const;

    /** Execute parallel for loop.

        Functor is called once for each value in the range
        [0, numberOfIterations), using the ThreadGroup.

        @param numberOfIterations The number of times to loop.

        @param f The functor to call for each loop index.
    */
    /** @{ */
    template <class Functor>
    void loopf (int numberOfIterations, Functor const& f)
    {
        IterationType <Functor> iteration (f);

        doLoop (numberOfIterations, iteration);
    }

#if BEAST_VARIADIC_MAX >= 1
    template <class Fn>
    void loop (int n, Fn f)
    { loopf (n, functional::bind (f, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 2
    template <class Fn, class T1>
    void loop (int n, Fn f, T1 t1)
    { loopf (n, functional::bind (f, t1, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 3
    template <class Fn, class T1, class T2>
    void loop (int n, Fn f, T1 t1, T2 t2)
    { loopf (n, functional::bind (f, t1, t2, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 4
    template <class Fn, class T1, class T2, class T3>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3)
    { loopf (n, functional::bind (f, t1, t2, t3, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 5
    template <class Fn, class T1, class T2, class T3, class T4>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4)
    { loopf (n, functional::bind (f, t1, t2, t3, t4, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 6
    template <class Fn, class T1, class T2, class T3, class T4, class T5>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
    { loopf (n, functional::bind (f, t1, t2, t3, t4, t5, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 7
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
    { loopf (n, functional::bind (f, t1, t2, t3, t4, t5, t6, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 8
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7)
    { loopf (n, functional::bind (f, t1, t2, t3, t4, t5, t6, t7, placeholders::_1)); }
#endif

#if BEAST_VARIADIC_MAX >= 9
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
    { loopf (n, functional::bind (f, t1, t2, t3, t4, t5, t6, t7, t8, placeholders::_1)); }
#endif
    /** @} */

private:
    class Iteration
    {
    public:
        virtual ~Iteration () { }
        virtual void operator () (int loopIndex) = 0;
    };

    template <class Functor>
    class IterationType : public Iteration, public Uncopyable
    {
    public:
        explicit IterationType (Functor const& f) : m_f (f)
        {
        }

        void operator () (int loopIndex)
        {
            m_f (loopIndex);
        }

    private:
        Functor m_f;
    };

private:
    class LoopState
        : public AllocatedBy <ThreadGroup::AllocatorType>
        , public Uncopyable
    {
    private:
        Iteration& m_iteration;
        WaitableEvent& m_finishedEvent;
        int const m_numberOfIterations;
        Atomic <int> m_loopIndex;
        Atomic <int> m_iterationsRemaining;
        Atomic <int> m_numberOfParallelInstances;

    public:
        LoopState (Iteration& iteration,
                   WaitableEvent& finishedEvent,
                   int numberOfIterations,
                   int numberOfParallelInstances)
            : m_iteration (iteration)
            , m_finishedEvent (finishedEvent)
            , m_numberOfIterations (numberOfIterations)
            , m_loopIndex (-1)
            , m_iterationsRemaining (numberOfIterations)
            , m_numberOfParallelInstances (numberOfParallelInstances)
        {
        }

        ~LoopState ()
        {
        }

        void forLoopBody ()
        {
            for (;;)
            {
                // Request a loop index to process.
                int const loopIndex = ++m_loopIndex;

                // Is it in range?
                if (loopIndex < m_numberOfIterations)
                {
                    // Yes, so process it.
                    m_iteration (loopIndex);

                    // Was this the last work item to complete?
                    if (--m_iterationsRemaining == 0)
                    {
                        // Yes, signal.
                        m_finishedEvent.signal ();
                        break;
                    }
                }
                else
                {
                    // Out of range, all work is complete or assigned.
                    break;
                }
            }

            release ();
        }

        void release ()
        {
            if (--m_numberOfParallelInstances == 0)
                delete this;
        }
    };

private:
    void doLoop (int numberOfIterations, Iteration& iteration);

private:
    ThreadGroup& m_pool;
    WaitableEvent m_finishedEvent;
    Atomic <int> m_currentIndex;
    Atomic <int> m_numberOfInstances;
};

#endif
