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

#include <beast/asio/shared_handler.h>

// Disables is_constructible tests for std::function
// Visual Studio std::function fails the is_constructible tests
#ifndef BEAST_NO_STD_FUNCTION_CONSTRUCTIBLE
# ifdef _MSC_VER
#  define BEAST_NO_STD_FUNCTION_CONSTRUCTIBLE 1
# else
#  define BEAST_NO_STD_FUNCTION_CONSTRUCTIBLE 0
# endif
#endif

namespace beast {

class shared_handler_test : public unit_test::suite
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
            std::size_t size, test_handler* h)
        {
            h->results.get().alloc = true;
            return boost::asio::asio_handler_allocate (size);
        }

        friend void asio_handler_deallocate (
            void* p, std::size_t size, test_handler* h)
        {
            h->results.get().dealloc = true;
            boost::asio::asio_handler_deallocate (p, size);
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
        handler();
        boost_asio_handler_alloc_helpers::deallocate (p, 32, handler);
        return boost_asio_handler_cont_helpers::is_continuation (handler);
    }

    void virtual_async_op (asio::shared_handler <void(void)> handler)
    {
        async_op (handler);
    }

    void run()
    {
    #if ! BEAST_NO_STD_FUNCTION_CONSTRUCTIBLE
        static_assert (! std::is_constructible <
            std::function <void(void)>, int&&>::value,
                "Cannot construct std::function from int&&");

        static_assert (! std::is_constructible <
            std::function <void(void)>, int>::value,
                "Cannot construct std::function from int");

        static_assert (! std::is_constructible <
            asio::shared_handler <void(void)>, int>::value,
                "Cannot construct shared_handler from int");
    #endif

        static_assert (std::is_constructible <
            asio::shared_handler <void(int)>,
                asio::shared_handler <void(int)>>::value,
                    "Should construct <void(int)> from <void(int)>");

        static_assert (! std::is_constructible <
            asio::shared_handler <void(int)>,
                asio::shared_handler <void(void)>>::value,
                    "Can't construct <void(int)> from <void(void)>");

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

        // Use of std::function shows the hooks not getting called
        {
            test_results r;
            std::function <void(void)> fh ((test_handler) (r));

            async_op (fh);
            expect (r.call);
            unexpected (r.alloc);
            unexpected (r.dealloc);
            unexpected (r.cont);

            test_invokable f;
            boost_asio_handler_invoke_helpers::invoke (std::ref (f), fh);
            unexpected (r.invoke);
            expect (f.call);
        }

        // Make sure shared_handler calls the hooks
        {
            test_results r;
            asio::shared_handler <void(void)> sh ((test_handler)(r));

            async_op (sh);
            expect (r.call);
            expect (r.alloc);
            expect (r.dealloc);
            expect (r.cont);

            test_invokable f;
            boost_asio_handler_invoke_helpers::invoke (std::ref (f), sh);
            expect (r.invoke);
            expect (f.call);
        }

        // Make sure shared_handler via implicit conversion calls hooks
        {
            test_results r;
            test_handler h (r);

            virtual_async_op ((test_handler) (r));
            expect (r.call);
            expect (r.alloc);
            expect (r.dealloc);
            expect (r.cont);
        }
    }
};

BEAST_DEFINE_TESTSUITE(shared_handler,asio,beast);

}
