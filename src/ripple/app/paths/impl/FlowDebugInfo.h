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

#ifndef RIPPLE_PATH_IMPL_FLOWDEBUGINFO_H_INCLUDED
#define RIPPLE_PATH_IMPL_FLOWDEBUGINFO_H_INCLUDED

#include <ripple/app/paths/impl/AmountSpec.h>
#include <ripple/ledger/PaymentSandbox.h>
#include <ripple/protocol/IOUAmount.h>
#include <ripple/protocol/XRPAmount.h>

#include <boost/container/flat_map.hpp>
#include <boost/optional.hpp>

#include <chrono>
#include <sstream>

namespace ripple
{
namespace path
{
namespace detail
{
// Track performance information of a single payment
struct FlowDebugInfo
{
    using clock = std::chrono::high_resolution_clock;
    using time_point = clock::time_point;
    boost::container::flat_map<std::string, std::pair<time_point, time_point>>
        timePoints;
    boost::container::flat_map<std::string, std::size_t> counts;

    struct PassInfo
    {
        PassInfo () = delete;
        PassInfo (bool nativeIn_, bool nativeOut_)
            : nativeIn (nativeIn_), nativeOut (nativeOut_)
        {
        }
        bool const nativeIn;
        bool const nativeOut;
        std::vector<EitherAmount> in;
        std::vector<EitherAmount> out;
        std::vector<size_t> numActive;

        std::vector<std::vector<EitherAmount>> liquiditySrcIn;
        std::vector<std::vector<EitherAmount>> liquiditySrcOut;

        void
        reserve (size_t s)
        {
            in.reserve (s);
            out.reserve (s);
            liquiditySrcIn.reserve(s);
            liquiditySrcOut.reserve(s);
            numActive.reserve (s);
        }

        size_t
        size () const
        {
            return in.size ();
        }

        void
        push_back (EitherAmount const& in_amt,
            EitherAmount const& out_amt,
            std::size_t active)
        {
            in.push_back (in_amt);
            out.push_back (out_amt);
            numActive.push_back (active);
        }

        void
        pushLiquiditySrc (EitherAmount const& in, EitherAmount const& out)
        {
            assert(!liquiditySrcIn.empty());
            liquiditySrcIn.back().push_back(in);
            liquiditySrcOut.back().push_back(out);
        }

        void
        newLiquidityPass()
        {
            auto const s = liquiditySrcIn.size();
            size_t const r = !numActive.empty() ? numActive.back() : 16;
            liquiditySrcIn.resize(s+1);
            liquiditySrcIn.back().reserve(r);
            liquiditySrcOut.resize(s+1);
            liquiditySrcOut.back().reserve(r);
        }
    };

    PassInfo passInfo;

    FlowDebugInfo () = delete;
    FlowDebugInfo (bool nativeIn, bool nativeOut)
        : passInfo (nativeIn, nativeOut)
    {
        timePoints.reserve (16);
        counts.reserve (16);
        passInfo.reserve (64);
    }

    auto
    duration (std::string const& tag) const
    {
        auto i = timePoints.find (tag);
        if (i == timePoints.end ())
        {
            assert (0);
            return std::chrono::duration<double>(0);
        }
        auto const& t = i->second;
        return std::chrono::duration_cast<std::chrono::duration<double>> (
            t.second - t.first);
    }

    std::size_t
    count (std::string const& tag) const
    {
        auto i = counts.find (tag);
        if (i == counts.end ())
            return 0;
        return i->second;
    }

    // Time the duration of the existence of the result
    auto
    timeBlock (std::string name)
    {
        struct Stopper
        {
            std::string tag;
            FlowDebugInfo* info;
            Stopper (std::string name, FlowDebugInfo& pi)
                : tag (std::move (name)), info (&pi)
            {
                auto const start = FlowDebugInfo::clock::now ();
                info->timePoints.emplace (tag, std::make_pair (start, start));
            }
            ~Stopper ()
            {
                auto const end = FlowDebugInfo::clock::now ();
                info->timePoints[tag].second = end;
            }
            Stopper(Stopper&&) = default;
        };
        return Stopper (std::move (name), *this);
    }

    void
    inc (std::string const& tag)
    {
        auto i = counts.find (tag);
        if (i == counts.end ())
        {
            counts[tag] = 1;
        }
        ++i->second;
    }

    void
    setCount (std::string const& tag, std::size_t c)
    {
        counts[tag] = c;
    }

    std::size_t
    passCount () const
    {
        return passInfo.size ();
    }

    void
    pushPass (EitherAmount const& in,
        EitherAmount const& out,
        std::size_t activeStrands)
    {
        passInfo.push_back (in, out, activeStrands);
    }

    void
    pushLiquiditySrc (EitherAmount const& in, EitherAmount const& out)
    {
        passInfo.pushLiquiditySrc (in, out);
    }

    void
    newLiquidityPass ()
    {
        passInfo.newLiquidityPass ();
    }

    std::string
    to_string (bool writePassInfo) const
    {
        std::ostringstream ostr;

        auto const d = duration ("main");

        ostr << "duration: " << d.count () << ", pass_count: " << passCount ();

        if (writePassInfo)
        {
            auto write_list = [&ostr](auto const& vals, auto&& fun, char delim=';') {
                ostr << '[';
                if (!vals.empty ())
                {
                    ostr << fun (vals[0]);
                    for (size_t i = 1, e = vals.size (); i < e; ++i)
                        ostr << delim << fun (vals[i]);
                }
                ostr << ']';
            };
            auto writeXrpAmtList = [&write_list](
                std::vector<EitherAmount> const& amts, char delim=';') {
                auto get_val = [](EitherAmount const& a) -> std::string {
                    return ripple::to_string (a.xrp);
                };
                write_list (amts, get_val, delim);
            };
            auto writeIouAmtList = [&write_list](
                std::vector<EitherAmount> const& amts, char delim=';') {
                auto get_val = [](EitherAmount const& a) -> std::string {
                    return ripple::to_string (a.iou);
                };
                write_list (amts, get_val, delim);
            };
            auto writeIntList = [&write_list](
                std::vector<size_t> const& vals, char delim=';') {
                auto get_val = [](
                    size_t const& v) -> size_t const& { return v; };
                write_list (vals, get_val);
            };
            auto writeNestedIouAmtList = [&ostr, &writeIouAmtList](
                std::vector<std::vector<EitherAmount>> const& amts) {
                ostr << '[';
                if (!amts.empty ())
                {
                    writeIouAmtList(amts[0], '|');
                    for (size_t i = 1, e = amts.size (); i < e; ++i)
                    {
                        ostr << ';';
                        writeIouAmtList(amts[i], '|');
                    }
                }
                ostr << ']';
            };
            auto writeNestedXrpAmtList = [&ostr, &writeXrpAmtList](
                std::vector<std::vector<EitherAmount>> const& amts) {
                ostr << '[';
                if (!amts.empty ())
                {
                    writeXrpAmtList(amts[0], '|');
                    for (size_t i = 1, e = amts.size (); i < e; ++i)
                    {
                        ostr << ';';
                        writeXrpAmtList(amts[i], '|');
                    }
                }
                ostr << ']';
            };

            ostr << ", in_pass: ";
            if (passInfo.nativeIn)
                writeXrpAmtList (passInfo.in);
            else
                writeIouAmtList (passInfo.in);
            ostr << ", out_pass: ";
            if (passInfo.nativeOut)
                writeXrpAmtList (passInfo.out);
            else
                writeIouAmtList (passInfo.out);
            ostr << ", num_active: ";
            writeIntList (passInfo.numActive);
            if (!passInfo.liquiditySrcIn.empty () &&
                !passInfo.liquiditySrcIn.back ().empty ())
            {
                ostr << ", l_src_in: ";
                if (passInfo.nativeIn)
                    writeNestedXrpAmtList (passInfo.liquiditySrcIn);
                else
                    writeNestedIouAmtList (passInfo.liquiditySrcIn);
                ostr << ", l_src_out: ";
                if (passInfo.nativeOut)
                    writeNestedXrpAmtList (passInfo.liquiditySrcOut);
                else
                    writeNestedIouAmtList (passInfo.liquiditySrcOut);
            }
        }

        return ostr.str ();
    }
};

inline
void
writeDiffElement (std::ostringstream& ostr,
    std::pair<std::tuple<AccountID, AccountID, Currency>, STAmount> const& elem)
{
    using namespace std;
    auto const k = elem.first;
    auto const v = elem.second;
    ostr << '[' << get<0> (k) << '|' << get<1> (k) << '|' << get<2> (k) << '|'
         << v << ']';
};

template<class Iter>
void
writeDiffs (std::ostringstream& ostr, Iter begin, Iter end)
{
    ostr << '[';
    if (begin != end)
    {
        writeDiffElement (ostr, *begin);
        ++begin;
    }
    for (; begin != end; ++begin)
    {
        ostr << ';';
        writeDiffElement (ostr, *begin);
    }
    ostr << ']';
};

using BalanceDiffs = std::pair<
    std::map<std::tuple<AccountID, AccountID, Currency>, STAmount>,
    XRPAmount>;

inline
BalanceDiffs
balanceDiffs(PaymentSandbox const& sb, ReadView const& rv)
{
    return {sb.balanceChanges (rv), sb.xrpDestroyed ()};
}

inline
std::string
balanceDiffsToString (boost::optional<BalanceDiffs> const& bd)
{
    if (!bd)
        return std::string{};
    auto const& diffs = bd->first;
    auto const& xrpDestroyed = bd->second;
    std::ostringstream ostr;
    ostr << ", xrpDestroyed: " << to_string (xrpDestroyed);
    ostr << ", balanceDiffs: ";
    writeDiffs (ostr, diffs.begin (), diffs.end ());
    return ostr.str ();
};

}  // detail
}  // path
}  // ripple
#endif
