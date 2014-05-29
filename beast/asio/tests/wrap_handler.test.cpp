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

#if BEAST_INCLUDE_BEASTCONFIG
#include <BeastConfig.h>
#endif

#include <beast/unit_test/suite.h>

#include <beast/asio/wrap_handler.h>

#include <boost/version.hpp>

#include <boost/bind.hpp>
#include <functional>
#include <memory>

namespace beast {
namespace asio {

//------------------------------------------------------------------------------

// Displays the order of destruction of parameters in the bind wrapper
//
class boost_bind_test : public unit_test::suite
{
public:
    struct Result
    {
        std::string text;

        void push_back (std::string const& s)
        {
            if (! text.empty())
                text += ", ";
            text += s;
        }
    };

    struct Payload
    {
        std::reference_wrapper <Result> m_result;
        std::string m_name;

        explicit Payload (Result& result, std::string const& name)
            : m_result (result)
            , m_name (name)
        {
        }

        ~Payload ()
        {
            m_result.get().push_back (m_name);
        }
    };

    struct Arg
    {
        std::shared_ptr <Payload> m_payload;

        Arg (Result& result, std::string const& name)
            : m_payload (std::make_shared <Payload> (result, name))
        {
        }
    };

    static void foo (Arg const&, Arg const&, Arg const&)
    {
    }

    void run()
    {
        {
            Result r;
            {
                boost::bind (&foo,
                    Arg (r, "one"),
                    Arg (r, "two"),
                    Arg (r, "three"));
            }
            log <<
                std::string ("boost::bind (") + r.text + ")";
        }

        {
            Result r;
            {
                std::bind (&foo,
                    Arg (r, "one"),
                    Arg (r, "two"),
                    Arg (r, "three"));
            }
            
            log <<
                std::string ("std::bind (") + r.text + ")";
        }

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(boost_bind,asio,beast);

//------------------------------------------------------------------------------

class wrap_handler_test : public unit_test::suite
{
public:
    struct test_results
    {
        bool call;
        bool invoke;
        bool alloc;
        bool dealloc;
        bool cont;

        test_results ()
            : call (false)
            , invoke (false)
            , alloc (false)
            , dealloc (false)
            , cont (false)
        {
        }
    };

    struct test_handler
    {
        std::reference_wrapper <test_results> results;

        explicit test_handler (test_results& results_)
            : results (results_)
        {
        }

        void operator() ()
        {
            results.get().call = true;
        }

        template <class Function>
        friend void asio_handler_invoke (
            Function& f, test_handler* h)
        {
            h->results.get().invoke = true;
            f();
        }

        template <class Function>
        friend void asio_handler_invoke (
            Function const& f, test_handler* h)
        {
            h->results.get().invoke = true;
            f();
        }

        friend void* asio_handler_allocate (
            std::size_t, test_handler* h)
        {
            h->results.get().alloc = true;
            return nullptr;
        }

        friend void asio_handler_deallocate (
            void*, std::size_t, test_handler* h)
        {
            h->results.get().dealloc = true;
        }

        friend bool asio_handler_is_continuation (
            test_handler* h)
        {
            h->results.get().cont = true;
            return true;
        }
    };

    struct test_invokable
    {
        bool call;

        test_invokable ()
            : call (false)
        {
        }

        void operator() ()
        {
            call = true;
        }
    };

    template <class Handler>
    bool async_op (Handler&& handler)
    {
        void* const p (boost_asio_handler_alloc_helpers::allocate (32, handler));
        (handler)();
        boost_asio_handler_alloc_helpers::deallocate (p, 32, handler);
        return boost_asio_handler_cont_helpers::is_continuation (handler);
    }

    void run()
    {
        // Hooks called when using the raw handler
        {
            test_results r;
            test_handler h (r);

            async_op (h);
            expect (r.call);
            expect (r.alloc);
            expect (r.dealloc);
            expect (r.cont);

            test_invokable f;
            boost_asio_handler_invoke_helpers::invoke (std::ref (f), h);
            expect (r.invoke);
            expect (f.call);
        }

        // Use of boost::bind shows the hooks not getting called
        {
            test_results r;
            test_handler h (r);
            auto b (std::bind (&test_handler::operator(), &h));

            async_op (b);
            expect (r.call);
            unexpected (r.alloc);
            unexpected (r.dealloc);
            unexpected (r.cont);

            test_invokable f;
            boost_asio_handler_invoke_helpers::invoke (std::ref (f), b);
            unexpected (r.invoke);
            expect (f.call);
        }

        // Make sure the wrapped handler calls the hooks
        {
            test_results r;
            test_handler h (r);
            auto w (wrap_handler (
                std::bind (&test_handler::operator(), test_handler(r)), h));

            async_op (w);
            expect (r.call);
            expect (r.alloc);
            expect (r.dealloc);
            expect (r.cont);

            test_invokable f;
            boost_asio_handler_invoke_helpers::invoke (std::ref (f), w);
            expect (r.invoke);
            expect (f.call);
        }
    }
};

BEAST_DEFINE_TESTSUITE(wrap_handler,asio,beast);

}
}

