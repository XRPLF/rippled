/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

#ifndef BEAST_PARALLELFOR_BEASTHEADER
#define BEAST_PARALLELFOR_BEASTHEADER

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
class ParallelFor : Uncopyable
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

    template <class F, class T1>
    void operator () (int numberOfIterations, T1 t1)
    {
    }

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

#if VFLIB_VARIADIC_MAX >= 1
    template <class Fn>
    void loop (int n, Fn f)
    {
        loopf (n, vf::bind (f, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 2
    template <class Fn, class T1>
    void loop (int n, Fn f, T1 t1)
    {
        loopf (n, vf::bind (f, t1, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 3
    template <class Fn, class T1, class T2>
    void loop (int n, Fn f, T1 t1, T2 t2)
    {
        loopf (n, vf::bind (f, t1, t2, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 4
    template <class Fn, class T1, class T2, class T3>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3)
    {
        loopf (n, vf::bind (f, t1, t2, t3, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 5
    template <class Fn, class T1, class T2, class T3, class T4>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4)
    {
        loopf (n, vf::bind (f, t1, t2, t3, t4, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 6
    template <class Fn, class T1, class T2, class T3, class T4, class T5>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5)
    {
        loopf (n, vf::bind (f, t1, t2, t3, t4, t5, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 7
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6)
    {
        loopf (n, vf::bind (f, t1, t2, t3, t4, t5, t6, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 8
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6, class T7>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7)
    {
        loopf (n, vf::bind (f, t1, t2, t3, t4, t5, t6, t7, vf::_1));
    }
#endif

#if VFLIB_VARIADIC_MAX >= 9
    template <class Fn, class T1, class T2, class T3, class T4, class T5, class T6, class T7, class T8>
    void loop (int n, Fn f, T1 t1, T2 t2, T3 t3, T4 t4, T5 t5, T6 t6, T7 t7, T8 t8)
    {
        loopf (n, vf::bind (f, t1, t2, t3, t4, t5, t6, t7, t8, vf::_1));
    }
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
    class IterationType : public Iteration, Uncopyable
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
        , Uncopyable
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
    int m_numberOfIterations;
};

//------------------------------------------------------------------------------

class ParallelFor2 : Uncopyable
{
public:
    /** Create a parallel for loop.

        It is best to keep this object around instead of creating and destroying
        it every time you need to run a loop.

        @param pool The ThreadGroup to use. If this is omitted then a singleton
                    ThreadGroup is used which contains one thread per CPU.
    */
    explicit ParallelFor2 (ThreadGroup& pool = *GlobalThreadGroup::getInstance ())
        : m_pool (pool)
        , m_finishedEvent (false) // auto-reset
    {
    }

    /** Determine the number of threads in the group.

        @return The number of threads in the group.
    */
    int getNumberOfThreads () const
    {
        return m_pool.getNumberOfThreads ();
    }


    template <class F, class T1, class T2, class T3, class T4>
    void operator () (int numberOfIterations, T1 t1, T2 t2, T3 t3, T4 t4)
    {
        Factory4 <F, T1, T2, T3, T4> f (t1, t2, t3, t4);
        doLoop (numberOfIterations, f);
    }

private:
    typedef ThreadGroup::AllocatorType AllocatorType;

    //---

    struct Iterator : public AllocatedBy <AllocatorType>
    {
        virtual ~Iterator () { }
        virtual void operator () (int loopIndex) = 0;
    };

    //---

    template <class Functor>
    struct IteratorType : public Iterator, Uncopyable
    {
        explicit IteratorType (Functor f) : m_f (f)
        {
        }

        void operator () (int loopIndex)
        {
            m_f (loopIndex);
        }

    private:
        Functor m_f;
    };

    //---

    struct Factory
    {
        virtual ~Factory () { }
        virtual Iterator* operator () (AllocatorType& allocator) = 0;
    };

    template <class F, class T1, class T2, class T3, class T4>
    struct Factory4 : Factory
    {
        Factory4 (T1 t1, T2 t2, T3 t3, T4 t4)
            : m_t1 (t1), m_t2 (t2), m_t3 (t3), m_t4 (t4) { }

        Iterator* operator () (AllocatorType& allocator)
        {
            return new (allocator) IteratorType <F> (m_t1, m_t2, m_t3, m_t4);
        }

    private:
        T1 m_t1;
        T2 m_t2;
        T3 m_t3;
        T4 m_t4;
    };

private:
    class LoopState
        : public AllocatedBy <AllocatorType>
        , Uncopyable
    {
    private:
        Factory& m_factory;
        WaitableEvent& m_finishedEvent;
        int const m_numberOfIterations;
        Atomic <int> m_loopIndex;
        Atomic <int> m_iterationsRemaining;
        Atomic <int> m_numberOfParallelInstances;
        AllocatorType& m_allocator;

    public:
        LoopState (Factory& factory,
                   WaitableEvent& finishedEvent,
                   int numberOfIterations,
                   int numberOfParallelInstances,
                   AllocatorType& allocator)
            : m_factory (factory)
            , m_finishedEvent (finishedEvent)
            , m_numberOfIterations (numberOfIterations)
            , m_loopIndex (-1)
            , m_iterationsRemaining (numberOfIterations)
            , m_numberOfParallelInstances (numberOfParallelInstances)
            , m_allocator (allocator)
        {
        }

        ~LoopState ()
        {
        }

        void forLoopBody ()
        {
            Iterator* iterator = m_factory (m_allocator);

            for (;;)
            {
                // Request a loop index to process.
                int const loopIndex = ++m_loopIndex;

                // Is it in range?
                if (loopIndex < m_numberOfIterations)
                {
                    // Yes, so process it.
                    (*iterator) (loopIndex);

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

            delete iterator;
        }

        void release ()
        {
            if (--m_numberOfParallelInstances == 0)
                delete this;
        }
    };

private:
    void doLoop (int numberOfIterations, Factory& factory)
    {
        if (numberOfIterations > 1)
        {
            int const numberOfThreads = m_pool.getNumberOfThreads ();

            // The largest number of pool threads we need is one less than the number
            // of iterations, because we also run the loop body on the caller's thread.
            //
            int const maxThreads = numberOfIterations - 1;

            // Calculate the number of parallel instances as the smaller of the number
            // of threads available (including the caller's) and the number of iterations.
            //
            int const numberOfParallelInstances = std::min (
                    numberOfThreads + 1, numberOfIterations);

            LoopState* loopState (new (m_pool.getAllocator ()) LoopState (
                                      factory,
                                      m_finishedEvent,
                                      numberOfIterations,
                                      numberOfParallelInstances,
                                      m_pool.getAllocator ()));

            m_pool.call (maxThreads, &LoopState::forLoopBody, loopState);

            // Also use the caller's thread to run the loop body.
            loopState->forLoopBody ();

            m_finishedEvent.wait ();
        }
        else if (numberOfIterations == 1)
        {
            // Just one iteration, so do it.
            Iterator* iter = factory (m_pool.getAllocator ());
            (*iter) (0);
            delete iter;
        }
    }

private:
    ThreadGroup& m_pool;
    WaitableEvent m_finishedEvent;
    Atomic <int> m_currentIndex;
    Atomic <int> m_numberOfInstances;
    int m_numberOfIterations;
};

#endif
