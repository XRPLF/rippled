//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2012, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/threads/Stoppable.h>

namespace beast {

Stoppable::Stoppable (char const* name, RootStoppable& root)
    : m_name (name)
    , m_root (root)
    , m_child (this)
    , m_started (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
}

Stoppable::Stoppable (char const* name, Stoppable& parent)
    : m_name (name)
    , m_root (parent.m_root)
    , m_child (this)
    , m_started (false)
    , m_stopped (false)
    , m_childrenStopped (false)
{
    // Must not have stopping parent.
    bassert (! parent.isStopping());

    parent.m_children.push_front (&m_child);
}

Stoppable::~Stoppable ()
{
    // Children must be stopped.
    bassert (!m_started || m_childrenStopped);
}

bool Stoppable::isStopping() const
{
    return m_root.isStopping();
}

bool Stoppable::isStopped () const
{
    return m_stopped;
}

bool Stoppable::areChildrenStopped () const
{
    return m_childrenStopped;
}

void Stoppable::stopped ()
{
    m_stoppedEvent.signal();
}

void Stoppable::onPrepare ()
{
}

void Stoppable::onStart ()
{
}

void Stoppable::onStop ()
{
    stopped();
}

void Stoppable::onChildrenStopped ()
{
}

//------------------------------------------------------------------------------

void Stoppable::prepareRecursive ()
{
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->prepareRecursive ();
    onPrepare ();
}

void Stoppable::startRecursive ()
{
    onStart ();
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->startRecursive ();
}

void Stoppable::stopAsyncRecursive ()
{
    onStop ();
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->stopAsyncRecursive ();
}

void Stoppable::stopRecursive (Journal journal)
{
    // Block on each child from the bottom of the tree up.
    //
    for (Children::const_iterator iter (m_children.cbegin ());
        iter != m_children.cend(); ++iter)
        iter->stoppable->stopRecursive (journal);

    // if we get here then all children have stopped
    //
    m_childrenStopped = true;
    onChildrenStopped ();

    // Now block on this Stoppable.
    //
    bool const timedOut (! m_stoppedEvent.wait (1 * 1000)); // milliseconds
    if (timedOut)
    {
        journal.warning << "Waiting for '" << m_name << "' to stop";
        m_stoppedEvent.wait ();
    }

    // once we get here, we know the stoppable has stopped.
    m_stopped = true;
}

//------------------------------------------------------------------------------

RootStoppable::RootStoppable (char const* name)
    : Stoppable (name, *this)
    , m_prepared (false)
    , m_calledStop (false)
    , m_calledStopAsync (false)
{
}

bool RootStoppable::isStopping() const
{
    return m_calledStopAsync;
}

void RootStoppable::prepare ()
{
    if (m_prepared.exchange (true) == false)
        prepareRecursive ();
}

void RootStoppable::start ()
{
    // Courtesy call to prepare.
    if (m_prepared.exchange (true) == false)
        prepareRecursive ();

    if (m_started.exchange (true) == false)
        startRecursive ();
}

void RootStoppable::stop (Journal journal)
{
    // Must have a prior call to start()
    bassert (m_started);

    if (m_calledStop.exchange (true) == true)
    {
        journal.warning << "Stoppable::stop called again";
        return;
    }

    stopAsync ();
    stopRecursive (journal);
}

void RootStoppable::stopAsync ()
{
    if (m_calledStopAsync.exchange (true) == false)
        stopAsyncRecursive ();
}

class Stoppable_test
    : public unit_test::suite
{
/*
            R
          / | \
       /    |  \
      A     B   C
    / | \   /\  |
    D E F  G H  I
      |
      J
*/
    unsigned count = 0;

    class D
        : public Stoppable
    {
        Stoppable_test& test_;
    public:
        D(Stoppable& parent, Stoppable_test& test)
            : Stoppable("D", parent)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 9, "D::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 0, "D::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 11, "D::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 2, "D::onChildrenStopped called out of order");
        }
    };

    class J
        : public Stoppable
    {
        Stoppable_test& test_;
    public:
        J(Stoppable& parent, Stoppable_test& test)
            : Stoppable("J", parent)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 7, "J::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 1, "J::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 10, "J::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 4, "J::onChildrenStopped called out of order");
        }
    };

    class E
        : public Stoppable
    {
        J j_;
        Stoppable_test& test_;
    public:
        E(Stoppable& parent, Stoppable_test& test)
            : Stoppable("E", parent)
            , j_(*this, test)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 8, "E::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 2, "E::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 9, "E::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 3, "E::onChildrenStopped called out of order");
        }
    };

    class F
        : public Stoppable
    {
        Stoppable_test& test_;
    public:
        F(Stoppable& parent, Stoppable_test& test)
            : Stoppable("F", parent)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 6, "F::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 3, "F::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 8, "F::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 5, "F::onChildrenStopped called out of order");
        }
    };

    class A
        : public Stoppable
    {
        enum {running, please_stop, have_stopped};
        D d_;
        E e_;
        F f_;
        Stoppable_test& test_;
        std::atomic<int> stop_;
    public:
        A(Stoppable& parent, Stoppable_test& test)
            : Stoppable("A", parent)
            , d_(*this, test)
            , e_(*this, test)
            , f_(*this, test)
            , test_(test)
            , stop_(running)
        {}

        void run()
        {
            while (stop_ == running)
                ;
            stop_ = have_stopped;
        }

        void onPrepare() override
        {
            test_.expect(++test_.count == 10, "A::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 4, "A::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 7, "A::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            stop_ = please_stop;
            while (stop_ != have_stopped)
                ;
            Stoppable::stopped();
            test_.expect(--test_.count == 1, "A::onChildrenStopped called out of order");
        }
    };

    class G
        : public Stoppable
    {
        Stoppable_test& test_;
    public:
        G(Stoppable& parent, Stoppable_test& test)
            : Stoppable("G", parent)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 4, "G::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 5, "G::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 6, "G::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 7, "G::onChildrenStopped called out of order");
        }
    };

    class H
        : public Stoppable
    {
        Stoppable_test& test_;
    public:
        H(Stoppable& parent, Stoppable_test& test)
            : Stoppable("H", parent)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 3, "H::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 6, "H::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 5, "H::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 8, "H::onChildrenStopped called out of order");
        }
    };

    class B
        : public Stoppable
    {
        G g_;
        H h_;
        Stoppable_test& test_;
    public:
        B(Stoppable& parent, Stoppable_test& test)
            : Stoppable("B", parent)
            , g_(*this, test)
            , h_(*this, test)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 5, "B::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 7, "B::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 4, "B::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 6, "B::onChildrenStopped called out of order");
        }
    };

    class I
        : public Stoppable
    {
        Stoppable_test& test_;
    public:
        I(Stoppable& parent, Stoppable_test& test)
            : Stoppable("I", parent)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 1, "I::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 8, "I::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 3, "I::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 10, "I::onChildrenStopped called out of order");
        }
    };

    class C
        : public Stoppable
    {
        I i_;
        Stoppable_test& test_;
    public:
        C(Stoppable& parent, Stoppable_test& test)
            : Stoppable("C", parent)
            , i_(*this, test)
            , test_(test)
        {}

        void onPrepare() override
        {
            test_.expect(++test_.count == 2, "C::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 9, "C::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 2, "C::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            Stoppable::stopped();
            test_.expect(--test_.count == 9, "C::onChildrenStopped called out of order");
        }
    };

    class Root
        : public RootStoppable
    {
        std::thread a_;
        B b_;
        C c_;
        Stoppable_test& test_;
    public:
        Root(Stoppable_test& test)
            : RootStoppable("R")
            , a_(&A::run, std::make_unique<A>(*this, test))
            , b_(*this, test)
            , c_(*this, test)
            , test_(test)
        {}

        void run()
        {
            prepare();
            start();
            stop();
        }

        void onPrepare() override
        {
            test_.expect(++test_.count == 11, "Root::onPrepare called out of order");
        }

        void onStart() override
        {
            test_.expect(--test_.count == 10, "Root::onStart called out of order");
        }

        void onStop() override
        {
            test_.expect(++test_.count == 1, "Root::onStop called out of order");
        }

        void onChildrenStopped() override
        {
            a_.join();
            Stoppable::stopped();
            test_.expect(--test_.count == 0, "Root::onChildrenStopped called out of order");
        }
    };

public:
    void run()
    {
        {
        Root rt(*this);
        rt.run();
        }
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(Stoppable,beast_core,beast);

}
