//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2014, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/nudb/verify.h>
#include <beast/nudb/tests/common.h>
#include <beast/unit_test/suite.h>
#include <beast/chrono/basic_seconds_clock.h>
#include <chrono>
#include <iomanip>
#include <ostream>

namespace beast {
namespace nudb {
namespace test {

namespace detail {

class save_stream_state
{
    std::ostream& os_;
    std::streamsize precision_;
    std::ios::fmtflags flags_;
    std::ios::char_type fill_;
public:
    ~save_stream_state()
    {
        os_.precision(precision_);
        os_.flags(flags_);
        os_.fill(fill_);
    }
    save_stream_state(save_stream_state const&) = delete;
    save_stream_state& operator=(save_stream_state const&) = delete;
    explicit save_stream_state(std::ostream& os)
        : os_(os)
        , precision_(os.precision())
        , flags_(os.flags())
        , fill_(os.fill())
    {
    }
};

template <class Rep, class Period>
std::ostream&
pretty_time(std::ostream& os, std::chrono::duration<Rep, Period> d)
{
    save_stream_state _(os);
    using namespace std::chrono;
    if (d < microseconds{1})
    {
        // use nanoseconds
        if (d < nanoseconds{100})
        {
            // use floating
            using ns = duration<float, std::nano>;
            os << std::fixed << std::setprecision(1) << ns(d).count();
        }
        else
        {
            // use integral
            os << std::chrono::duration_cast<nanoseconds>(d).count();
        }
        os << "ns";
    }
    else if (d < milliseconds{1})
    {
        // use microseconds
        if (d < microseconds{100})
        {
            // use floating
            using ms = duration<float, std::micro>;
            os << std::fixed << std::setprecision(1) << ms(d).count();
        }
        else
        {
            // use integral
            os << std::chrono::duration_cast<microseconds>(d).count();
        }
        os << "us";
    }
    else if (d < seconds{1})
    {
        // use milliseconds
        if (d < milliseconds{100})
        {
            // use floating
            using ms = duration<float, std::milli>;
            os << std::fixed << std::setprecision(1) << ms(d).count();
        }
        else
        {
            // use integral
            os << std::chrono::duration_cast<milliseconds>(d).count();
        }
        os << "ms";
    }
    else if (d < minutes{1})
    {
        // use seconds
        if (d < seconds{100})
        {
            // use floating
            using s = duration<float>;
            os << std::fixed << std::setprecision(1) << s(d).count();
        }
        else
        {
            // use integral
            os << std::chrono::duration_cast<seconds>(d).count();
        }
        os << "s";
    }
    else
    {
        // use minutes
        if (d < minutes{100})
        {
            // use floating
            using m = duration<float, std::ratio<60>>;
            os << std::fixed << std::setprecision(1) << m(d).count();
        }
        else
        {
            // use integral
            os << std::chrono::duration_cast<minutes>(d).count();
        }
        os << "min";
    }
    return os;
}

template <class Period, class Rep>
inline
std::string
fmtdur(std::chrono::duration<Period, Rep> const& d)
{
    std::stringstream ss;
    pretty_time(ss, d);
    return ss.str();
}

} // detail

//------------------------------------------------------------------------------

template <class Log>
class progress
{
private:
    using clock_type =
        beast::basic_seconds_clock<
            std::chrono::steady_clock>;

    Log& log_;
    clock_type::time_point start_ = clock_type::now();
    clock_type::time_point now_ = clock_type::now();
    clock_type::time_point report_ = clock_type::now();
    std::size_t prev_ = 0;
    bool estimate_ = false;

public:
    explicit
    progress(Log& log)
        : log_(log)
    {
    }

    void
    operator()(std::size_t w, std::size_t w1)
    {
        using namespace std::chrono;
        auto const now = clock_type::now();
        if (now == now_)
            return;
        now_ = now;
        auto const elapsed = now - start_;
        if (! estimate_)
        {
            if (elapsed < seconds(15))
                return;
            estimate_ = true;
        }
        else if (now - report_ <
            std::chrono::seconds(60))
        {
            return;
        }
        auto const rate =
            elapsed.count() / double(w);
        clock_type::duration const remain(
            static_cast<clock_type::duration::rep>(
                (w1 - w) * rate));
        log_ <<
            "Remaining: " << detail::fmtdur(remain) <<
                " (" << w << " of " << w1 <<
                    " in " << detail::fmtdur(elapsed) <<
                ", " << (w - prev_) <<
                    " in " << detail::fmtdur(now - report_) <<
                ")";
        report_ = now;
        prev_ = w;
    }

    void
    finish()
    {
        log_ <<
            "Total time: " << detail::fmtdur(
                clock_type::now() - start_);
    }
};

//------------------------------------------------------------------------------

class verify_test : public unit_test::suite
{
public:
    // Runs verify on the database and reports statistics
    void
    do_verify (nudb::path_type const& path)
    {
        auto const dp = path + ".dat";
        auto const kp = path + ".key";
        print(log, test_api::verify(dp, kp));
    }

    void
    run() override
    {
        testcase(abort_on_fail) << "verify " << arg();
        if (arg().empty())
            return fail("missing unit test argument");
        do_verify(arg());
        pass();
    }
};

class verify_fast_test : public unit_test::suite
{
public:
    // Runs verify on the database and reports statistics
    void
    do_verify (nudb::path_type const& path)
    {
        auto const dp = path + ".dat";
        auto const kp = path + ".key";
        progress<decltype(log)> p(log);
        // VFALCO HACK 32gb hardcoded!
        auto const info = verify_fast<
            test_api::hash_type>(
                dp, kp, 34359738368, p);
        print(log, info);
    }

    void
    run() override
    {
        testcase(abort_on_fail) << "verify_fast " << arg();
        if (arg().empty())
            return fail("missing unit test argument");
        do_verify(arg());
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(verify,nudb,beast);
BEAST_DEFINE_TESTSUITE_MANUAL(verify_fast,nudb,beast);

} // test
} // nudb
} // beast
