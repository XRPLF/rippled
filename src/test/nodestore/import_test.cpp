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

#include <ripple/basics/contract.h>
#include <ripple/beast/clock/basic_seconds_clock.h>
#include <ripple/beast/core/LexicalCast.h>
#include <ripple/beast/rfc2616.h>
#include <ripple/beast/unit_test.h>
#include <ripple/nodestore/impl/codec.h>
#include <boost/beast/core/string.hpp>
#include <boost/regex.hpp>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <map>
#include <nudb/create.hpp>
#include <nudb/detail/format.hpp>
#include <nudb/xxhasher.hpp>
#include <sstream>


/*

Math:

1000 gb dat file
170 gb key file
capacity 113 keys/bucket

normal:
1,000gb data file read
19,210gb key file read (113 * 170)
19,210gb key file write

multi(32gb):
6 passes (170/32)
6,000gb data file read
170gb key file write


*/

namespace ripple {
namespace NodeStore {

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
    save_stream_state&
    operator=(save_stream_state const&) = delete;
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
            os << round<nanoseconds>(d).count();
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
            os << round<microseconds>(d).count();
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
            os << round<milliseconds>(d).count();
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
            os << round<seconds>(d).count();
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
            os << round<minutes>(d).count();
        }
        os << "min";
    }
    return os;
}

template <class Period, class Rep>
inline std::string
fmtdur(std::chrono::duration<Period, Rep> const& d)
{
    std::stringstream ss;
    pretty_time(ss, d);
    return ss.str();
}

}  // namespace detail

//------------------------------------------------------------------------------

class progress
{
private:
    using clock_type = beast::basic_seconds_clock;

    std::size_t const work_;
    clock_type::time_point start_ = clock_type::now();
    clock_type::time_point now_ = clock_type::now();
    clock_type::time_point report_ = clock_type::now();
    std::size_t prev_ = 0;
    bool estimate_ = false;

public:
    explicit progress(std::size_t work) : work_(work)
    {
    }

    template <class Log>
    void
    operator()(Log& log, std::size_t work)
    {
        using namespace std::chrono;
        auto const now = clock_type::now();
        if (now == now_)
            return;
        now_ = now;
        auto const elapsed = now - start_;
        if (!estimate_)
        {
            if (elapsed < seconds(15))
                return;
            estimate_ = true;
        }
        else if (now - report_ < std::chrono::seconds(60))
        {
            return;
        }
        auto const rate = elapsed.count() / double(work);
        clock_type::duration const remain(
            static_cast<clock_type::duration::rep>((work_ - work) * rate));
        log << "Remaining: " << detail::fmtdur(remain) << " (" << work << " of "
            << work_ << " in " << detail::fmtdur(elapsed) << ", "
            << (work - prev_) << " in " << detail::fmtdur(now - report_) << ")";
        report_ = now;
        prev_ = work;
    }

    template <class Log>
    void
    finish(Log& log)
    {
        log << "Total time: " << detail::fmtdur(clock_type::now() - start_);
    }
};

std::map<std::string, std::string, boost::beast::iless>
parse_args(std::string const& s)
{
    // <key> '=' <value>
    static boost::regex const re1(
        "^"                        // start of line
        "(?:\\s*)"                 // whitespace (optonal)
        "([a-zA-Z][_a-zA-Z0-9]*)"  // <key>
        "(?:\\s*)"                 // whitespace (optional)
        "(?:=)"                    // '='
        "(?:\\s*)"                 // whitespace (optional)
        "(.*\\S+)"                 // <value>
        "(?:\\s*)"                 // whitespace (optional)
        ,
        boost::regex_constants::optimize);
    std::map<std::string, std::string, boost::beast::iless> map;
    auto const v = beast::rfc2616::split(s.begin(), s.end(), ',');
    for (auto const& kv : v)
    {
        boost::smatch m;
        if (!boost::regex_match(kv, m, re1))
            Throw<std::runtime_error>("invalid parameter " + kv);
        auto const result = map.emplace(m[1], m[2]);
        if (!result.second)
            Throw<std::runtime_error>("duplicate parameter " + m[1]);
    }
    return map;
}

}  // namespace NodeStore
}  // namespace ripple
