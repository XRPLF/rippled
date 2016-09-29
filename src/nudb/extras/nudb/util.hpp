//
// Copyright (c) 2015-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef UTIL_HPP
#define UTIL_HPP

#include "basic_seconds_clock.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace nudb {

template<class = void>
int
log2(std::uint64_t n)
{
    int i = -(n == 0);

    auto const S =
        [&](int k)
        {
            if(n >=(std::uint64_t{1} << k))
            {
                i += k;
                n >>= k;
            }
        };
    S(32); S(16); S(8); S(4); S(2); S(1);
    return i;
}

// Format a decimal integer with comma separators
template<class T>
std::string
fdec(T t)
{
    std::string s = std::to_string(t);
    std::reverse(s.begin(), s.end());
    std::string s2;
    s2.reserve(s.size() +(s.size()+2)/3);
    int n = 0;
    for(auto c : s)
    {
        if(n == 3)
        {
            n = 0;
            s2.insert(s2.begin(), ',');
        }
        ++n;
        s2.insert(s2.begin(), c);
    }
    return s2;
}

// format 64-bit unsigned as fixed width, 0 padded hex
template<class T>
std::string
fhex(T v)
{
    std::string s{"0x0000000000000000"};
    auto it = s.end();
    for(it = s.end(); v; v >>= 4)
        *--it = "0123456789abcdef"[v & 0xf];
    return s;
}

// Format an array of integers as a comma separated list
template<class T, std::size_t N>
static
std::string
fhist(std::array<T, N> const& hist)
{
    std::size_t n;
    for(n = hist.size() - 1; n > 0; --n)
        if(hist[n])
            break;
    std::string s = std::to_string(hist[0]);
    for(std::size_t i = 1; i <= n; ++i)
        s += ", " + std::to_string(hist[i]);
    return s;
}

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

template<class Rep, class Period>
std::ostream&
pretty_time(std::ostream& os, std::chrono::duration<Rep, Period> d)
{
    save_stream_state _(os);
    using namespace std::chrono;
    if(d < microseconds{1})
    {
        // use nanoseconds
        if(d < nanoseconds{100})
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
    else if(d < milliseconds{1})
    {
        // use microseconds
        if(d < microseconds{100})
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
    else if(d < seconds{1})
    {
        // use milliseconds
        if(d < milliseconds{100})
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
    else if(d < minutes{1})
    {
        // use seconds
        if(d < seconds{100})
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
        if(d < minutes{100})
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

template<class Period, class Rep>
std::string
fmtdur(std::chrono::duration<Period, Rep> const& d)
{
    std::stringstream ss;
    pretty_time(ss, d);
    return ss.str();
}

//------------------------------------------------------------------------------

class progress
{
    using clock_type = basic_seconds_clock<std::chrono::steady_clock>;

    std::ostream& os_;
    clock_type::time_point start_;
    clock_type::time_point now_;
    clock_type::time_point report_;
    std::uint64_t prev_;
    bool estimate_;

public:
    explicit
    progress(std::ostream& os)
        : os_(os)
    {
    }

    void
    operator()(std::uint64_t amount, std::uint64_t total)
    {
        using namespace std::chrono;
        auto const now = clock_type::now();
        if(amount == 0)
        {
            now_ = clock_type::now();
            start_ = now_;
            report_ = now_;
            prev_ = 0;
            estimate_ = false;
            return;
        }
        if(now == now_)
            return;
        now_ = now;
        auto const elapsed = now - start_;
        if(! estimate_)
        {
            // Wait a bit before showing the first estimate
            if(elapsed < seconds{30})
                return;
            estimate_ = true;
        }
        else if(now - report_ < seconds{60})
        {
            // Only show estimates periodically
            return;
        }
        auto const rate = double(amount) / elapsed.count();
        auto const remain = clock_type::duration{
            static_cast<clock_type::duration::rep>(
               (total - amount) / rate)};
        os_ <<
            "Remaining: " << fmtdur(remain) <<
                " (" << fdec(amount) << " of " << fdec(total) <<
                    " in " << fmtdur(elapsed) <<
                ", " << fdec(amount - prev_) <<
                    " in " << fmtdur(now - report_) <<
                ")\n";
        report_ = now;
        prev_ = amount;
    }

    clock_type::duration
    elapsed() const
    {
        using namespace std::chrono;
        return now_ - start_;
    }
};

} // nudb

#endif
