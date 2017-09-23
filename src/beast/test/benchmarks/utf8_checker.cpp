//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <beast/websocket/detail/utf8_checker.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/locale.hpp>
#include <chrono>
#include <random>

namespace beast {

class utf8_checker_test : public beast::unit_test::suite
{
    std::mt19937 rng_;

public:
    using size_type = std::uint64_t;

    class timer
    {
        using clock_type =
            std::chrono::system_clock;

        clock_type::time_point when_;

    public:
        using duration =
            clock_type::duration;

        timer()
            : when_(clock_type::now())
        {
        }

        duration
        elapsed() const
        {
            return clock_type::now() - when_;
        }
    };

    template<class UInt = std::size_t>
    UInt
    rand(std::size_t n)
    {
        return static_cast<UInt>(
            std::uniform_int_distribution<
                std::size_t>{0, n-1}(rng_));
    }

    static
    inline
    size_type
    throughput(std::chrono::duration<
        double> const& elapsed, size_type items)
    {
        using namespace std::chrono;
        return static_cast<size_type>(
            1 / (elapsed/items).count());
    }

    std::string
    corpus(std::size_t n)
    {
        std::string s;
        s.reserve(n);
        while(n--)
            s.push_back(static_cast<char>(
                ' ' + rand(95)));
        return s;
    }

    void
    checkLocale(std::string const& s)
    {
        using namespace boost::locale;
        auto p = s.begin();
        auto const e = s.end();
        while(p != e)
        {
            auto cp = utf::utf_traits<char>::decode(p, e);
            if(cp == utf::illegal)
                break;
        }
    }

    void
    checkBeast(std::string const& s)
    {
        beast::websocket::detail::check_utf8(
            s.data(), s.size());
    }

    template<class F>
    typename timer::clock_type::duration
    test(F const& f)
    {
        timer t;
        f();
        return t.elapsed();
    }

    void
    run() override
    {
        auto const s = corpus(32 * 1024 * 1024);
        for(int i = 0; i < 5; ++ i)
        {
            auto const elapsed = test([&]{
                checkBeast(s);
                checkBeast(s);
                checkBeast(s);
                checkBeast(s);
                checkBeast(s);
            });
            log << "beast:  " << throughput(elapsed, s.size()) << " char/s" << std::endl;
        }
        for(int i = 0; i < 5; ++ i)
        {
            auto const elapsed = test([&]{
                checkLocale(s);
                checkLocale(s);
                checkLocale(s);
                checkLocale(s);
                checkLocale(s);
            });
            log << "locale: " << throughput(elapsed, s.size()) << " char/s" << std::endl;
        }
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(utf8_checker,benchmarks,beast);

} // beast

