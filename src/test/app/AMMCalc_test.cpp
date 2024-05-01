//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <ripple/app/misc/AMMHelpers.h>
#include <ripple/protocol/Quality.h>
#include <test/jtx.h>

#include <boost/regex.hpp>

namespace ripple {
namespace test {

/** AMM Calculator. Uses AMM formulas to simulate the payment engine
 * expected results. Assuming the formulas are correct some unit-tests can
 * be verified. Currently supported operations are:
 *  - swapIn, find out given in. in can flow through multiple AMM/Offer steps.
 *  - swapOut, find in given out. out can flow through multiple AMM/Offer steps.
 *  - lptokens, find lptokens given pool composition.
 *  - changespq, change AMM spot price (SP) quality. given AMM and Offer
 *      find out AMM offer, which changes AMM's SP quality to
 *      the Offer's quality.
 */
class AMMCalc_test : public beast::unit_test::suite
{
    using token_iter = boost::sregex_token_iterator;
    using steps = std::vector<std::pair<Amounts, bool>>;
    using trates = std::map<std::string, std::uint32_t>;
    using swapargs = std::tuple<steps, STAmount, trates, std::uint32_t>;
    jtx::Account const gw{jtx::Account("gw")};
    token_iter const end_;

    std::optional<STAmount>
    getAmt(token_iter const& p, bool* delimited = nullptr)
    {
        using namespace jtx;
        if (p == end_)
            return STAmount{};
        std::string str = *p;
        str = boost::regex_replace(str, boost::regex("^(A|O)[(]"), "");
        boost::smatch match;
        // XXX(val))?
        boost::regex rx("^([^(]+)[(]([^)]+)[)]([)])?$");
        if (boost::regex_search(str, match, rx))
        {
            if (delimited)
                *delimited = (match[3] != "");
            if (match[1] == "XRP")
                return XRP(std::stoll(match[2]));
            // drops
            else if (match[1] == "XRPA")
                return XRPAmount{std::stoll(match[2])};
            return amountFromString(gw[match[1]], match[2]);
        }
        return std::nullopt;
    }

    std::optional<std::tuple<std::string, std::uint32_t, bool>>
    getRate(token_iter const& p)
    {
        if (p == end_)
            return std::nullopt;
        std::string str = *p;
        str = boost::regex_replace(str, boost::regex("^T[(]"), "");
        // XXX(rate))?
        boost::smatch match;
        boost::regex rx("^([^(]+)[(]([^)]+)[)]([)])?$");
        if (boost::regex_search(str, match, rx))
        {
            std::string const currency = match[1];
            // input is rate * 100, no fraction
            std::uint32_t rate = 10'000'000 * std::stoi(match[2].str());
            // true if delimited - )
            return {{currency, rate, match[3] != "" ? true : false}};
        }
        return std::nullopt;
    }

    std::uint32_t
    getFee(token_iter const& p)
    {
        if (p != end_)
        {
            std::string const s = *p;
            return std::stoll(s);
        }
        return 0;
    }

    std::optional<std::pair<Amounts, bool>>
    getAmounts(token_iter& p)
    {
        if (p == end_)
            return std::nullopt;
        std::string const s = *p;
        bool const amm = s[0] == 'O' ? false : true;
        auto const a1 = getAmt(p++);
        if (!a1 || p == end_)
            return std::nullopt;
        auto const a2 = getAmt(p++);
        if (!a2)
            return std::nullopt;
        return {{{*a1, *a2}, amm}};
    }

    std::optional<trates>
    getTransferRate(token_iter& p)
    {
        trates rates{};
        if (p == end_)
            return rates;
        std::string str = *p;
        if (str[0] != 'T')
            return rates;
        // T(USD(rate),GBP(rate), ...)
        while (p != end_)
        {
            if (auto const rate = getRate(p++))
            {
                auto const [currency, trate, delimited] = *rate;
                rates[currency] = trate;
                if (delimited)
                    break;
            }
            else
                return std::nullopt;
        }
        return rates;
    }

    std::optional<swapargs>
    getSwap(token_iter& p)
    {
        // pairs of amm pool or offer
        steps pairs;
        // either amm pool or offer
        auto isPair = [](auto const& p) {
            std::string const s = *p;
            return s[0] == 'A' || s[0] == 'O';
        };
        // get AMM or offer
        while (isPair(p))
        {
            auto const res = getAmounts(p);
            if (!res || p == end_)
                return std::nullopt;
            pairs.push_back(*res);
        }
        // swap in/out amount
        auto const swap = getAmt(p++);
        if (!swap)
            return std::nullopt;
        // optional transfer rate
        auto const rate = getTransferRate(p);
        if (!rate)
            return std::nullopt;
        auto const fee = getFee(p);
        return {{pairs, *swap, *rate, fee}};
    }

    std::string
    toString(STAmount const& a)
    {
        std::stringstream str;
        str << a.getText() << "/" << to_string(a.issue().currency);
        return str.str();
    }

    STAmount
    mulratio(STAmount const& amt, std::uint32_t a, std::uint32_t b, bool round)
    {
        if (a == b)
            return amt;
        if (amt.native())
            return toSTAmount(mulRatio(amt.xrp(), a, b, round), amt.issue());
        return toSTAmount(mulRatio(amt.iou(), a, b, round), amt.issue());
    }

    void
    swapOut(swapargs const& args)
    {
        auto const vp = std::get<steps>(args);
        STAmount sout = std::get<STAmount>(args);
        auto const fee = std::get<std::uint32_t>(args);
        auto const rates = std::get<trates>(args);
        STAmount resultOut = sout;
        STAmount resultIn{};
        STAmount sin{};
        int limitingStep = vp.size();
        STAmount limitStepOut{};
        auto trate = [&](auto const& amt) {
            auto const currency = to_string(amt.issue().currency);
            return rates.find(currency) != rates.end() ? rates.at(currency)
                                                       : QUALITY_ONE;
        };
        // swap out reverse
        sin = sout;
        for (auto it = vp.rbegin(); it != vp.rend(); ++it)
        {
            sout = mulratio(sin, trate(sin), QUALITY_ONE, true);
            auto const [amts, amm] = *it;
            // assume no amm limit
            if (amm)
            {
                sin = swapAssetOut(amts, sout, fee);
            }
            else if (sout <= amts.out)
            {
                sin = Quality{amts}.ceil_out(amts, sout).in;
            }
            // limiting step
            else
            {
                sin = amts.in;
                limitingStep = vp.rend() - it - 1;
                limitStepOut = amts.out;
                if (it == vp.rbegin())
                    resultOut = amts.out;
            }
            resultIn = sin;
        }
        sin = limitStepOut;
        // swap in if limiting step
        for (int i = limitingStep + 1; i < vp.size(); ++i)
        {
            auto const [amts, amm] = vp[i];
            sin = mulratio(sin, QUALITY_ONE, trate(sin), false);
            if (amm)
            {
                sout = swapAssetIn(amts, sin, fee);
            }
            // assume there is no limiting step in fwd
            else
            {
                sout = Quality{amts}.ceil_in(amts, sin).out;
            }
            sin = sout;
            resultOut = sout;
        }
        std::cout << "in: " << toString(resultIn)
                  << " out: " << toString(resultOut) << std::endl;
    }

    void
    swapIn(swapargs const& args)
    {
        auto const vp = std::get<steps>(args);
        STAmount sin = std::get<STAmount>(args);
        auto const fee = std::get<std::uint32_t>(args);
        auto const rates = std::get<trates>(args);
        STAmount resultIn = sin;
        STAmount resultOut{};
        STAmount sout{};
        int limitingStep = 0;
        STAmount limitStepIn{};
        auto trate = [&](auto const& amt) {
            auto const currency = to_string(amt.issue().currency);
            return rates.find(currency) != rates.end() ? rates.at(currency)
                                                       : QUALITY_ONE;
        };
        // Swap in forward
        for (auto it = vp.begin(); it != vp.end(); ++it)
        {
            auto const [amts, amm] = *it;
            sin = mulratio(
                sin,
                QUALITY_ONE,
                trate(sin),
                false);  // out of the next step
            // assume no amm limit
            if (amm)
            {
                sout = swapAssetIn(amts, sin, fee);
            }
            else if (sin <= amts.in)
            {
                sout = Quality{amts}.ceil_in(amts, sin).out;
            }
            // limiting step, requested in is greater than the offer
            // pay exactly amts.in, which gets amts.out
            else
            {
                sout = amts.out;
                limitingStep = it - vp.begin();
                limitStepIn = amts.in;
            }
            sin = sout;
            resultOut = sout;
        }
        sin = limitStepIn;
        // swap out if limiting step
        for (int i = limitingStep - 1; i >= 0; --i)
        {
            sout = mulratio(sin, trate(sin), QUALITY_ONE, false);
            auto const [amts, amm] = vp[i];
            if (amm)
            {
                sin = swapAssetOut(amts, sout, fee);
            }
            // assume there is no limiting step
            else
            {
                sin = Quality{amts}.ceil_out(amts, sout).in;
            }
            resultIn = sin;
        }
        resultOut = mulratio(resultOut, QUALITY_ONE, trate(resultOut), true);
        std::cout << "in: " << toString(resultIn)
                  << " out: " << toString(resultOut) << std::endl;
    }

    void
    run() override
    {
        using namespace jtx;
        auto const a = arg();
        boost::regex re(",");
        token_iter p(a.begin(), a.end(), re, -1);
        // Token is denoted as CUR(xxx), where CUR is the currency code
        //    and xxx is the amount, for instance: XRP(100) or USD(11.5)
        // AMM is denoted as A(CUR1(xxx1),CUR2(xxx2)), for instance:
        //    A(XRP(1000),USD(1000)), the tokens must be in the order
        //    poolGets/poolPays
        // Offer is denoted as O(CUR1(xxx1),CUR2(xxx2)), for instance:
        //    O(XRP(100),USD(100)), the tokens must be in the order
        //    takerPays/takerGets
        // Transfer rate is denoted as a comma separated list for each
        // currency with the transfer rate, for instance:
        //   T(USD(175),...,EUR(100)).
        //   the transfer rate is 100 * rate, with no fraction, for instance:
        //     1.75 = 1.75 * 100 = 175
        //   the transfer rate is optional
        // AMM trading fee is an integer in {0,1000}, 1000 represents 1%
        //   the trading fee is optional
        auto const exec = [&]() -> bool {
            if (p == end_)
                return true;
            // Swap in to the steps. Execute steps in forward direction first.
            // swapin,A(XRP(1000),USD(1000)),O(USD(10),EUR(10)),XRP(11),
            //     T(USD(125)),1000
            // where
            //   A(...),O(...) are the payment steps, in this case
            //     consisting of AMM and Offer.
            //   XRP(11) is the swapIn value. Note the order of tokens in AMM;
            //     i.e. poolGets/poolPays.
            //   T(USD(125) is the transfer rate of 1.25%.
            //   1000 is AMM trading fee of 1%, the fee is optional.
            if (*p == "swapin")
            {
                if (auto const swap = getSwap(++p); swap)
                {
                    swapIn(*swap);
                    return true;
                }
            }
            // Swap out of the steps. Execute steps in reverse direction first.
            // swapout,A(USD(1000),XRP(1000)),XRP(10),T(USD(100)),100
            // where
            //   A(...) is the payment step, in this case
            //     consisting of AMM.
            //   XRP(10) is the swapOut value. Note the order of tokens in AMM:
            //     i.e. poolGets/poolPays.
            //   T(USD(100) is the transfer rate of 1%.
            //   100 is AMM trading fee of 0.1%.
            else if (*p == "swapout")
            {
                if (auto const swap = getSwap(++p); swap)
                {
                    swapOut(*swap);
                    return true;
                }
            }
            // Calculate AMM lptokens
            // lptokens,USD(1000),XRP(1000)
            // where
            //  USD(...),XRP(...) is the pool composition
            else if (*p == "lptokens")
            {
                if (auto const pool = getAmounts(++p); pool)
                {
                    Account const amm("amm");
                    auto const LPT = amm["LPT"];
                    std::cout
                        << to_string(
                               ammLPTokens(pool->first.in, pool->first.out, LPT)
                                   .iou())
                        << std::endl;
                    return true;
                }
            }
            // Change spot price quality - generates AMM offer such that
            // when consumed the updated AMM spot price quality is equal
            // to the CLOB offer quality
            // changespq,A(XRP(1000),USD(1000)),O(XRP(100),USD(99)),10
            //   where
            //     A(...) is AMM
            //     O(...) is CLOB offer
            //     10 is AMM trading fee
            else if (*p == "changespq")
            {
                Env env(*this);
                if (auto const pool = getAmounts(++p))
                {
                    if (auto const offer = getAmounts(p))
                    {
                        auto const fee = getFee(p);
                        if (auto const ammOffer = changeSpotPriceQuality(
                                pool->first,
                                Quality{offer->first},
                                fee,
                                env.current()->rules(),
                                beast::Journal(beast::Journal::getNullSink()));
                            ammOffer)
                            std::cout
                                << "amm offer: " << toString(ammOffer->in)
                                << " " << toString(ammOffer->out)
                                << "\nnew pool: "
                                << toString(pool->first.in + ammOffer->in)
                                << " "
                                << toString(pool->first.out - ammOffer->out)
                                << std::endl;
                        else
                            std::cout << "can't change the pool's SP quality"
                                      << std::endl;
                        return true;
                    }
                }
            }
            return false;
        };
        bool res = false;
        try
        {
            res = exec();
        }
        catch (std::exception const& ex)
        {
            std::cout << ex.what() << std::endl;
        }
        BEAST_EXPECT(res);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(AMMCalc, app, ripple);

}  // namespace test
}  // namespace ripple
