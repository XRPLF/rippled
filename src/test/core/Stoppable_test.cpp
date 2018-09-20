//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/core/Stoppable.h>
#include <ripple/beast/unit_test.h>
#include <test/unit_test/SuiteJournalSink.h>
#include <thread>

namespace ripple {
namespace test {

class Stoppable_test
    : public beast::unit_test::suite
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
        enum {running, please_stop, stopping, stopped};
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
        ~A() override
        {
            while (stop_ != stopped)
                ;
        }

        void run()
        {
            while (stop_ == running)
                ;
            stop_ = stopping;
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
            while (stop_ != stopping)
                ;
            Stoppable::stopped();
            test_.expect(--test_.count == 1, "A::onChildrenStopped called out of order");
            stop_ = stopped;
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
        SuiteJournalSink sink_;
        beast::Journal journal_;

    public:
        explicit Root(Stoppable_test& test)
            : RootStoppable("R")
            , a_(&A::run, std::make_unique<A>(*this, test))
            , b_(*this, test)
            , c_(*this, test)
            , test_(test)
            , sink_("Stoppable_test", beast::severities::kFatal, test)
            , journal_(sink_)
        {}

        void run()
        {
            prepare();
            start();
            stop (journal_);
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

        void secondStop()
        {
            // Calling stop() a second time should have no negative
            // consequences.
            stop (journal_);
        }
    };

public:
    void run() override
    {
        {
            Root rt(*this);
            rt.run();
            rt.secondStop();
        }
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(Stoppable,core,ripple);

}
}
