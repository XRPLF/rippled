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

#include <beast/utility/static_initializer.h>
#include <beast/unit_test/suite.h>
#include <atomic>
#include <sstream>
#include <thread>
#include <utility>

namespace beast {

static_assert(__alignof(long) >= 4, "");

class static_initializer_test : public unit_test::suite
{
public:
    // Used to create separate instances for each test
    struct cxx11_tag { };
    struct beast_tag { };
    template <std::size_t N, class Tag>
    struct Case
    {
        enum { count = N };
        typedef Tag type;
    };

    struct Counts
    {
        Counts()
            : calls (0)
            , constructed (0)
            , access (0)
        {
        }

        // number of calls to the constructor
        std::atomic <long> calls;

        // incremented after construction completes
        std::atomic <long> constructed;

        // increment when class is accessed before construction
        std::atomic <long> access;
    };

    /*  This testing singleton detects two conditions:
        1. Being accessed before getting fully constructed
        2. Getting constructed twice
    */
    template <class Tag>
    class Test;

    template <class Function>
    static
    void
    run_many (std::size_t n, Function f);

    template <class Tag>
    void
    test (cxx11_tag);

    template <class Tag>
    void
    test (beast_tag);

    template <class Tag>
    void
    test();

    void
    run ();
};

//------------------------------------------------------------------------------

template <class Tag>
class static_initializer_test::Test
{
public:
    explicit
    Test (Counts& counts);

    void
    operator() (Counts& counts);
};

template <class Tag>
static_initializer_test::Test<Tag>::Test (Counts& counts)
{
    ++counts.calls;
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
    ++counts.constructed;
}

template <class Tag>
void
static_initializer_test::Test<Tag>::operator() (Counts& counts)
{
    if (! counts.constructed)
        ++counts.access;
}

//------------------------------------------------------------------------------

template <class Function>
void
static_initializer_test::run_many (std::size_t n, Function f)
{
    std::atomic <bool> start (false);
    std::vector <std::thread> threads;

    threads.reserve (n);

    for (auto i (n); i-- ;)
    {
        threads.emplace_back([&]()
        {
            while (! start.load())
                std::this_thread::yield();
            f();
        });
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (10));
    std::this_thread::yield();
    start.store (true);
    for (auto& thread : threads)
        thread.join();
}

template <class Tag>
void
static_initializer_test::test (cxx11_tag)
{
    testcase << "cxx11 " << Tag::count << " threads";

    Counts counts;

    run_many (Tag::count, [&]()
    {
        static Test <Tag> t (counts);
        t(counts);
    });

#ifdef _MSC_VER
    // Visual Studio 2013 and earlier can exhibit both double
    // construction, and access before construction when function
    // local statics are initialized concurrently.
    //
    expect (counts.constructed > 1 || counts.access > 0);

#else
    expect (counts.constructed == 1 && counts.access == 0);

#endif
}

template <class Tag>
void
static_initializer_test::test (beast_tag)
{
    testcase << "beast " << Tag::count << " threads";

    Counts counts;

    run_many (Tag::count, [&counts]()
    {
        static static_initializer <Test <Tag>> t (counts);
        (*t)(counts);
    });

    expect (counts.constructed == 1 && counts.access == 0);
}

template <class Tag>
void
static_initializer_test::test()
{
    test <Tag> (typename Tag::type {});
}

void
static_initializer_test::run ()
{
    test <Case<  4, cxx11_tag>> ();
    test <Case< 16, cxx11_tag>> ();
    test <Case< 64, cxx11_tag>> ();
    test <Case<256, cxx11_tag>> ();

    test <Case<  4, beast_tag>> ();
    test <Case< 16, beast_tag>> ();
    test <Case< 64, beast_tag>> ();
    test <Case<256, beast_tag>> ();
}

//------------------------------------------------------------------------------

BEAST_DEFINE_TESTSUITE(static_initializer,utility,beast);

}
