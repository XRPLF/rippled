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

#include <test/jtx.h>
#include <test/jtx/PathSet.h>

#include <xrpld/app/paths/AMMContext.h>
#include <xrpld/app/paths/RippleCalc.h>
#include <xrpld/app/paths/detail/Steps.h>
#include <xrpld/core/Config.h>
#include <xrpld/ledger/PaymentSandbox.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/jss.h>

#include <optional>

namespace ripple {
namespace test {

struct DirectStepInfo
{
    AccountID src;
    AccountID dst;
    Currency currency;
};

struct XRPEndpointStepInfo
{
    AccountID acc;
};

enum class TrustFlag { freeze, auth, noripple };

/*constexpr*/ std::uint32_t
trustFlag(TrustFlag f, bool useHigh)
{
    switch (f)
    {
        case TrustFlag::freeze:
            if (useHigh)
                return lsfHighFreeze;
            return lsfLowFreeze;
        case TrustFlag::auth:
            if (useHigh)
                return lsfHighAuth;
            return lsfLowAuth;
        case TrustFlag::noripple:
            if (useHigh)
                return lsfHighNoRipple;
            return lsfLowNoRipple;
    }
    return 0;  // Silence warning about end of non-void function
}

bool
getTrustFlag(
    jtx::Env const& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    Currency const& cur,
    TrustFlag flag)
{
    if (auto sle = env.le(keylet::line(src, dst, cur)))
    {
        auto const useHigh = src.id() > dst.id();
        return sle->isFlag(trustFlag(flag, useHigh));
    }
    Throw<std::runtime_error>("No line in getTrustFlag");
    return false;  // silence warning
}

bool
equal(std::unique_ptr<Step> const& s1, DirectStepInfo const& dsi)
{
    if (!s1)
        return false;
    return test::directStepEqual(*s1, dsi.src, dsi.dst, dsi.currency);
}

bool
equal(std::unique_ptr<Step> const& s1, XRPEndpointStepInfo const& xrpsi)
{
    if (!s1)
        return false;
    return test::xrpEndpointStepEqual(*s1, xrpsi.acc);
}

bool
equal(std::unique_ptr<Step> const& s1, ripple::Book const& bsi)
{
    if (!s1)
        return false;
    return bookStepEqual(*s1, bsi);
}

template <class Iter>
bool
strandEqualHelper(Iter i)
{
    // base case. all args processed and found equal.
    return true;
}

template <class Iter, class StepInfo, class... Args>
bool
strandEqualHelper(Iter i, StepInfo&& si, Args&&... args)
{
    if (!equal(*i, std::forward<StepInfo>(si)))
        return false;
    return strandEqualHelper(++i, std::forward<Args>(args)...);
}

template <class... Args>
bool
equal(Strand const& strand, Args&&... args)
{
    if (strand.size() != sizeof...(Args))
        return false;
    if (strand.empty())
        return true;
    return strandEqualHelper(strand.begin(), std::forward<Args>(args)...);
}

STPathElement
ape(AccountID const& a)
{
    return STPathElement(
        STPathElement::typeAccount, a, xrpCurrency(), xrpAccount());
};

// Issue path element
STPathElement
ipe(Issue const& iss)
{
    return STPathElement(
        STPathElement::typeCurrency | STPathElement::typeIssuer,
        xrpAccount(),
        iss.currency,
        iss.account);
};

// Issuer path element
STPathElement
iape(AccountID const& account)
{
    return STPathElement(
        STPathElement::typeIssuer, xrpAccount(), xrpCurrency(), account);
};

class ElementComboIter
{
    enum class SB /*state bit*/
        : std::uint16_t {
            acc,
            iss,
            cur,
            rootAcc,
            rootIss,
            xrp,
            sameAccIss,
            existingAcc,
            existingCur,
            existingIss,
            prevAcc,
            prevCur,
            prevIss,
            boundary,
            last
        };

    std::uint16_t state_ = 0;
    static_assert(
        safe_cast<size_t>(SB::last) <= sizeof(decltype(state_)) * 8,
        "");
    STPathElement const* prev_ = nullptr;
    // disallow iss and cur to be specified with acc is specified (simplifies
    // some tests)
    bool const allowCompound_ = false;

    bool
    has(SB s) const
    {
        return state_ & (1 << safe_cast<int>(s));
    }

    bool
    hasAny(std::initializer_list<SB> sb) const
    {
        for (auto const s : sb)
            if (has(s))
                return true;
        return false;
    }

    size_t
    count(std::initializer_list<SB> sb) const
    {
        size_t result = 0;

        for (auto const s : sb)
            if (has(s))
                result++;
        return result;
    }

public:
    explicit ElementComboIter(STPathElement const* prev = nullptr) : prev_(prev)
    {
    }

    bool
    valid() const
    {
        return (allowCompound_ ||
                !(has(SB::acc) && hasAny({SB::cur, SB::iss}))) &&
            (!hasAny({SB::prevAcc, SB::prevCur, SB::prevIss}) || prev_) &&
            (!hasAny(
                 {SB::rootAcc, SB::sameAccIss, SB::existingAcc, SB::prevAcc}) ||
             has(SB::acc)) &&
            (!hasAny(
                 {SB::rootIss, SB::sameAccIss, SB::existingIss, SB::prevIss}) ||
             has(SB::iss)) &&
            (!hasAny({SB::xrp, SB::existingCur, SB::prevCur}) ||
             has(SB::cur)) &&
            // These will be duplicates
            (count({SB::xrp, SB::existingCur, SB::prevCur}) <= 1) &&
            (count({SB::rootAcc, SB::existingAcc, SB::prevAcc}) <= 1) &&
            (count({SB::rootIss, SB::existingIss, SB::rootIss}) <= 1);
    }
    bool
    next()
    {
        if (!(has(SB::last)))
        {
            do
            {
                ++state_;
            } while (!valid());
        }
        return !has(SB::last);
    }

    template <
        class Col,
        class AccFactory,
        class IssFactory,
        class CurrencyFactory>
    void
    emplace_into(
        Col& col,
        AccFactory&& accF,
        IssFactory&& issF,
        CurrencyFactory&& currencyF,
        std::optional<AccountID> const& existingAcc,
        std::optional<Currency> const& existingCur,
        std::optional<AccountID> const& existingIss)
    {
        assert(!has(SB::last));

        auto const acc = [&]() -> std::optional<AccountID> {
            if (!has(SB::acc))
                return std::nullopt;
            if (has(SB::rootAcc))
                return xrpAccount();
            if (has(SB::existingAcc) && existingAcc)
                return existingAcc;
            return accF().id();
        }();
        auto const iss = [&]() -> std::optional<AccountID> {
            if (!has(SB::iss))
                return std::nullopt;
            if (has(SB::rootIss))
                return xrpAccount();
            if (has(SB::sameAccIss))
                return acc;
            if (has(SB::existingIss) && existingIss)
                return *existingIss;
            return issF().id();
        }();
        auto const cur = [&]() -> std::optional<Currency> {
            if (!has(SB::cur))
                return std::nullopt;
            if (has(SB::xrp))
                return xrpCurrency();
            if (has(SB::existingCur) && existingCur)
                return *existingCur;
            return currencyF();
        }();
        if (!has(SB::boundary))
            col.emplace_back(acc, cur, iss);
        else
            col.emplace_back(
                STPathElement::Type::typeBoundary,
                acc.value_or(AccountID{}),
                cur.value_or(Currency{}),
                iss.value_or(AccountID{}));
    }
};

struct ExistingElementPool
{
    std::vector<jtx::Account> accounts;
    std::vector<ripple::Currency> currencies;
    std::vector<std::string> currencyNames;

    jtx::Account
    getAccount(size_t id)
    {
        assert(id < accounts.size());
        return accounts[id];
    }

    ripple::Currency
    getCurrency(size_t id)
    {
        assert(id < currencies.size());
        return currencies[id];
    }

    // ids from 0 through (nextAvail -1) have already been used in the
    // path
    size_t nextAvailAccount = 0;
    size_t nextAvailCurrency = 0;

    using ResetState = std::tuple<size_t, size_t>;
    ResetState
    getResetState() const
    {
        return std::make_tuple(nextAvailAccount, nextAvailCurrency);
    }

    void
    resetTo(ResetState const& s)
    {
        std::tie(nextAvailAccount, nextAvailCurrency) = s;
    }

    struct StateGuard
    {
        ExistingElementPool& p_;
        ResetState state_;

        explicit StateGuard(ExistingElementPool& p)
            : p_{p}, state_{p.getResetState()}
        {
        }
        ~StateGuard()
        {
            p_.resetTo(state_);
        }
    };

    // Create the given number of accounts, and add trust lines so every
    // account trusts every other with every currency
    // Create an offer from every currency/account to every other
    // currency/account; the offer owner is either the specified
    // account or the issuer of the "taker gets" account
    void
    setupEnv(
        jtx::Env& env,
        size_t numAct,
        size_t numCur,
        std::optional<size_t> const& offererIndex)
    {
        using namespace jtx;

        assert(!offererIndex || offererIndex < numAct);

        accounts.clear();
        accounts.reserve(numAct);
        currencies.clear();
        currencies.reserve(numCur);
        currencyNames.clear();
        currencyNames.reserve(numCur);

        constexpr size_t bufSize = 32;
        char buf[bufSize];

        for (size_t id = 0; id < numAct; ++id)
        {
            snprintf(buf, bufSize, "A%zu", id);
            accounts.emplace_back(buf);
        }

        for (size_t id = 0; id < numCur; ++id)
        {
            if (id < 10)
                snprintf(buf, bufSize, "CC%zu", id);
            else if (id < 100)
                snprintf(buf, bufSize, "C%zu", id);
            else
                snprintf(buf, bufSize, "%zu", id);
            currencies.emplace_back(to_currency(buf));
            currencyNames.emplace_back(buf);
        }

        for (auto const& a : accounts)
            env.fund(XRP(100000), a);

        // Every account trusts every other account with every currency
        for (auto ai1 = accounts.begin(), aie = accounts.end(); ai1 != aie;
             ++ai1)
        {
            for (auto ai2 = accounts.begin(); ai2 != aie; ++ai2)
            {
                if (ai1 == ai2)
                    continue;
                for (auto const& cn : currencyNames)
                {
                    env.trust((*ai1)[cn](1'000'000), *ai2);
                    if (ai1 > ai2)
                    {
                        // accounts with lower indexes hold balances from
                        // accounts
                        // with higher indexes
                        auto const& src = *ai1;
                        auto const& dst = *ai2;
                        env(pay(src, dst, src[cn](500000)));
                    }
                }
                env.close();
            }
        }

        std::vector<IOU> ious;
        ious.reserve(numAct * numCur);
        for (auto const& a : accounts)
            for (auto const& cn : currencyNames)
                ious.emplace_back(a[cn]);

        // create offers from every currency to every other currency
        for (auto takerPays = ious.begin(), ie = ious.end(); takerPays != ie;
             ++takerPays)
        {
            for (auto takerGets = ious.begin(); takerGets != ie; ++takerGets)
            {
                if (takerPays == takerGets)
                    continue;
                auto const owner =
                    offererIndex ? accounts[*offererIndex] : takerGets->account;
                if (owner.id() != takerGets->account.id())
                    env(pay(takerGets->account, owner, (*takerGets)(1000)));

                env(offer(owner, (*takerPays)(1000), (*takerGets)(1000)),
                    txflags(tfPassive));
            }
            env.close();
        }

        // create offers to/from xrp to every other ious
        for (auto const& iou : ious)
        {
            auto const owner =
                offererIndex ? accounts[*offererIndex] : iou.account;
            env(offer(owner, iou(1000), XRP(1000)), txflags(tfPassive));
            env(offer(owner, XRP(1000), iou(1000)), txflags(tfPassive));
            env.close();
        }
    }

    std::int64_t
    totalXRP(ReadView const& v, bool incRoot)
    {
        std::uint64_t totalXRP = 0;
        auto add = [&](auto const& a) {
            // XRP balance
            auto const sle = v.read(keylet::account(a));
            if (!sle)
                return;
            auto const b = (*sle)[sfBalance];
            totalXRP += b.mantissa();
        };
        for (auto const& a : accounts)
            add(a);
        if (incRoot)
            add(xrpAccount());
        return totalXRP;
    }

    // Check that the balances for all accounts for all currencies & XRP are the
    // same
    bool
    checkBalances(ReadView const& v1, ReadView const& v2)
    {
        std::vector<std::tuple<STAmount, STAmount, AccountID, AccountID>> diffs;

        auto xrpBalance = [](ReadView const& v, ripple::Keylet const& k) {
            auto const sle = v.read(k);
            if (!sle)
                return STAmount{};
            return (*sle)[sfBalance];
        };
        auto lineBalance = [](ReadView const& v, ripple::Keylet const& k) {
            auto const sle = v.read(k);
            if (!sle)
                return STAmount{};
            return (*sle)[sfBalance];
        };
        std::uint64_t totalXRP[2]{};
        for (auto ai1 = accounts.begin(), aie = accounts.end(); ai1 != aie;
             ++ai1)
        {
            {
                // XRP balance
                auto const ak = keylet::account(*ai1);
                auto const b1 = xrpBalance(v1, ak);
                auto const b2 = xrpBalance(v2, ak);
                totalXRP[0] += b1.mantissa();
                totalXRP[1] += b2.mantissa();
                if (b1 != b2)
                    diffs.emplace_back(b1, b2, xrpAccount(), *ai1);
            }
            for (auto ai2 = accounts.begin(); ai2 != aie; ++ai2)
            {
                if (ai1 >= ai2)
                    continue;
                for (auto const& c : currencies)
                {
                    // Line balance
                    auto const lk = keylet::line(*ai1, *ai2, c);
                    auto const b1 = lineBalance(v1, lk);
                    auto const b2 = lineBalance(v2, lk);
                    if (b1 != b2)
                        diffs.emplace_back(b1, b2, *ai1, *ai2);
                }
            }
        }
        return diffs.empty();
    }

    jtx::Account
    getAvailAccount()
    {
        return getAccount(nextAvailAccount++);
    }

    ripple::Currency
    getAvailCurrency()
    {
        return getCurrency(nextAvailCurrency++);
    }

    template <class F>
    void
    for_each_element_pair(
        STAmount const& sendMax,
        STAmount const& deliver,
        std::vector<STPathElement> const& prefix,
        std::vector<STPathElement> const& suffix,
        std::optional<AccountID> const& existingAcc,
        std::optional<Currency> const& existingCur,
        std::optional<AccountID> const& existingIss,
        F&& f)
    {
        auto accF = [&] { return this->getAvailAccount(); };
        auto issF = [&] { return this->getAvailAccount(); };
        auto currencyF = [&] { return this->getAvailCurrency(); };

        STPathElement const* prevOuter =
            prefix.empty() ? nullptr : &prefix.back();
        ElementComboIter outer(prevOuter);

        std::vector<STPathElement> outerResult;
        std::vector<STPathElement> result;
        auto const resultSize = prefix.size() + suffix.size() + 2;
        outerResult.reserve(resultSize);
        result.reserve(resultSize);
        while (outer.next())
        {
            StateGuard og{*this};
            outerResult = prefix;
            outer.emplace_into(
                outerResult,
                accF,
                issF,
                currencyF,
                existingAcc,
                existingCur,
                existingIss);
            STPathElement const* prevInner = &outerResult.back();
            ElementComboIter inner(prevInner);
            while (inner.next())
            {
                StateGuard ig{*this};
                result = outerResult;
                inner.emplace_into(
                    result,
                    accF,
                    issF,
                    currencyF,
                    existingAcc,
                    existingCur,
                    existingIss);
                result.insert(result.end(), suffix.begin(), suffix.end());
                f(sendMax, deliver, result);
            }
        };
    }
};

struct PayStrand_test : public beast::unit_test::suite
{
    void
    testToStrand(FeatureBitset features)
    {
        testcase("To Strand");

        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto const eurC = EUR.currency;
        auto const usdC = USD.currency;

        using D = DirectStepInfo;
        using B = ripple::Book;
        using XRPS = XRPEndpointStepInfo;

        AMMContext ammContext(alice, false);

        auto test = [&, this](
                        jtx::Env& env,
                        Issue const& deliver,
                        std::optional<Issue> const& sendMaxIssue,
                        STPath const& path,
                        TER expTer,
                        auto&&... expSteps) {
            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                bob,
                deliver,
                std::nullopt,
                sendMaxIssue,
                path,
                true,
                OfferCrossing::no,
                ammContext,
                std::nullopt,
                env.app().logs().journal("Flow"));
            BEAST_EXPECT(ter == expTer);
            if (sizeof...(expSteps) != 0)
                BEAST_EXPECT(equal(
                    strand, std::forward<decltype(expSteps)>(expSteps)...));
        };

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(USD(1000), alice, bob);
            env.trust(EUR(1000), alice, bob);
            env(pay(gw, alice, EUR(100)));

            {
                STPath const path =
                    STPath({ipe(bob["USD"]), cpe(EUR.currency)});
                auto [ter, _] = toStrand(
                    *env.current(),
                    alice,
                    alice,
                    /*deliver*/ xrpIssue(),
                    /*limitQuality*/ std::nullopt,
                    /*sendMaxIssue*/ EUR.issue(),
                    path,
                    true,
                    OfferCrossing::no,
                    ammContext,
                    std::nullopt,
                    env.app().logs().journal("Flow"));
                (void)_;
                BEAST_EXPECT(ter == tesSUCCESS);
            }
            {
                STPath const path = STPath({ipe(USD), cpe(xrpCurrency())});
                auto [ter, _] = toStrand(
                    *env.current(),
                    alice,
                    alice,
                    /*deliver*/ xrpIssue(),
                    /*limitQuality*/ std::nullopt,
                    /*sendMaxIssue*/ EUR.issue(),
                    path,
                    true,
                    OfferCrossing::no,
                    ammContext,
                    std::nullopt,
                    env.app().logs().journal("Flow"));
                (void)_;
                BEAST_EXPECT(ter == tesSUCCESS);
            }
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, carol, gw);

            test(env, USD, std::nullopt, STPath(), terNO_LINE);

            env.trust(USD(1000), alice, bob, carol);
            test(env, USD, std::nullopt, STPath(), tecPATH_DRY);

            env(pay(gw, alice, USD(100)));
            env(pay(gw, carol, USD(100)));

            // Insert implied account
            test(
                env,
                USD,
                std::nullopt,
                STPath(),
                tesSUCCESS,
                D{alice, gw, usdC},
                D{gw, bob, usdC});
            env.trust(EUR(1000), alice, bob);

            // Insert implied offer
            test(
                env,
                EUR,
                USD.issue(),
                STPath(),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{USD, EUR, std::nullopt},
                D{gw, bob, eurC});

            // Path with explicit offer
            test(
                env,
                EUR,
                USD.issue(),
                STPath({ipe(EUR)}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{USD, EUR, std::nullopt},
                D{gw, bob, eurC});

            // Path with offer that changes issuer only
            env.trust(carol["USD"](1000), bob);
            test(
                env,
                carol["USD"],
                USD.issue(),
                STPath({iape(carol)}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{USD, carol["USD"], std::nullopt},
                D{carol, bob, usdC});

            // Path with XRP src currency
            test(
                env,
                USD,
                xrpIssue(),
                STPath({ipe(USD)}),
                tesSUCCESS,
                XRPS{alice},
                B{XRP, USD, std::nullopt},
                D{gw, bob, usdC});

            // Path with XRP dst currency.
            test(
                env,
                xrpIssue(),
                USD.issue(),
                STPath({STPathElement{
                    STPathElement::typeCurrency,
                    xrpAccount(),
                    xrpCurrency(),
                    xrpAccount()}}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{USD, XRP, std::nullopt},
                XRPS{bob});

            // Path with XRP cross currency bridged payment
            test(
                env,
                EUR,
                USD.issue(),
                STPath({cpe(xrpCurrency())}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{USD, XRP, std::nullopt},
                B{XRP, EUR, std::nullopt},
                D{gw, bob, eurC});

            // XRP -> XRP transaction can't include a path
            test(env, XRP, std::nullopt, STPath({ape(carol)}), temBAD_PATH);

            {
                // The root account can't be the src or dst
                auto flowJournal = env.app().logs().journal("Flow");
                {
                    // The root account can't be the dst
                    auto r = toStrand(
                        *env.current(),
                        alice,
                        xrpAccount(),
                        XRP,
                        std::nullopt,
                        USD.issue(),
                        STPath(),
                        true,
                        OfferCrossing::no,
                        ammContext,
                        std::nullopt,
                        flowJournal);
                    BEAST_EXPECT(r.first == temBAD_PATH);
                }
                {
                    // The root account can't be the src
                    auto r = toStrand(
                        *env.current(),
                        xrpAccount(),
                        alice,
                        XRP,
                        std::nullopt,
                        std::nullopt,
                        STPath(),
                        true,
                        OfferCrossing::no,
                        ammContext,
                        std::nullopt,
                        flowJournal);
                    BEAST_EXPECT(r.first == temBAD_PATH);
                }
                {
                    // The root account can't be the src.
                    auto r = toStrand(
                        *env.current(),
                        noAccount(),
                        bob,
                        USD,
                        std::nullopt,
                        std::nullopt,
                        STPath(),
                        true,
                        OfferCrossing::no,
                        ammContext,
                        std::nullopt,
                        flowJournal);
                    BEAST_EXPECT(r.first == temBAD_PATH);
                }
            }

            // Create an offer with the same in/out issue
            test(
                env,
                EUR,
                USD.issue(),
                STPath({ipe(USD), ipe(EUR)}),
                temBAD_PATH);

            // Path element with type zero
            test(
                env,
                USD,
                std::nullopt,
                STPath({STPathElement(
                    0, xrpAccount(), xrpCurrency(), xrpAccount())}),
                temBAD_PATH);

            // The same account can't appear more than once on a path
            // `gw` will be used from alice->carol and implied between carol
            // and bob
            test(
                env,
                USD,
                std::nullopt,
                STPath({ape(gw), ape(carol)}),
                temBAD_PATH_LOOP);

            // The same offer can't appear more than once on a path
            test(
                env,
                EUR,
                USD.issue(),
                STPath({ipe(EUR), ipe(USD), ipe(EUR)}),
                temBAD_PATH_LOOP);
        }

        {
            // cannot have more than one offer with the same output issue

            using namespace jtx;
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);
            env.trust(EUR(10000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));
            env(pay(gw, bob, EUR(100)));

            env(offer(bob, XRP(100), USD(100)));
            env(offer(bob, USD(100), EUR(100)), txflags(tfPassive));
            env(offer(bob, EUR(100), USD(100)), txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
            env(pay(alice, carol, USD(100)),
                path(~USD, ~EUR, ~USD),
                sendmax(XRP(200)),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, noripple(gw));
            env.trust(USD(1000), alice, bob);
            env(pay(gw, alice, USD(100)));
            test(env, USD, std::nullopt, STPath(), terNO_RIPPLE);
        }

        {
            // check global freeze
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(USD(1000), alice, bob);
            env(pay(gw, alice, USD(100)));

            // Account can still issue payments
            env(fset(alice, asfGlobalFreeze));
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);
            env(fclear(alice, asfGlobalFreeze));
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);

            // Account can not issue funds
            env(fset(gw, asfGlobalFreeze));
            test(env, USD, std::nullopt, STPath(), terNO_LINE);
            env(fclear(gw, asfGlobalFreeze));
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);

            // Account can not receive funds
            env(fset(bob, asfGlobalFreeze));
            test(env, USD, std::nullopt, STPath(), terNO_LINE);
            env(fclear(bob, asfGlobalFreeze));
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);
        }
        {
            // Freeze between gw and alice
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(USD(1000), alice, bob);
            env(pay(gw, alice, USD(100)));
            test(env, USD, std::nullopt, STPath(), tesSUCCESS);
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            BEAST_EXPECT(getTrustFlag(env, gw, alice, usdC, TrustFlag::freeze));
            test(env, USD, std::nullopt, STPath(), terNO_LINE);
        }
        {
            // check no auth
            // An account may require authorization to receive IOUs from an
            // issuer
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfRequireAuth));
            env.trust(USD(1000), alice, bob);
            // Authorize alice but not bob
            env(trust(gw, alice["USD"](1000), tfSetfAuth));
            BEAST_EXPECT(getTrustFlag(env, gw, alice, usdC, TrustFlag::auth));
            env(pay(gw, alice, USD(100)));
            env.require(balance(alice, USD(100)));
            test(env, USD, std::nullopt, STPath(), terNO_AUTH);

            // Check pure issue redeem still works
            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                gw,
                USD,
                std::nullopt,
                std::nullopt,
                STPath(),
                true,
                OfferCrossing::no,
                ammContext,
                std::nullopt,
                env.app().logs().journal("Flow"));
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(equal(strand, D{alice, gw, usdC}));
        }

        {
            // last step xrp from offer
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(USD(1000), alice, bob);
            env(pay(gw, alice, USD(100)));

            // alice -> USD/XRP -> bob
            STPath path;
            path.emplace_back(std::nullopt, xrpCurrency(), std::nullopt);

            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                bob,
                XRP,
                std::nullopt,
                USD.issue(),
                path,
                false,
                OfferCrossing::no,
                ammContext,
                std::nullopt,
                env.app().logs().journal("Flow"));
            BEAST_EXPECT(ter == tesSUCCESS);
            BEAST_EXPECT(equal(
                strand,
                D{alice, gw, usdC},
                B{USD.issue(), xrpIssue(), std::nullopt},
                XRPS{bob}));
        }
    }

    void
    testRIPD1373(FeatureBitset features)
    {
        using namespace jtx;
        testcase("RIPD1373");

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);

            env.trust(USD(1000), alice, bob);
            env.trust(EUR(1000), alice, bob);
            env.trust(bob["USD"](1000), alice, gw);
            env.trust(bob["EUR"](1000), alice, gw);

            env(offer(bob, XRP(100), bob["USD"](100)), txflags(tfPassive));
            env(offer(gw, XRP(100), USD(100)), txflags(tfPassive));

            env(offer(bob, bob["USD"](100), bob["EUR"](100)),
                txflags(tfPassive));
            env(offer(gw, USD(100), EUR(100)), txflags(tfPassive));

            Path const p = [&] {
                Path result;
                result.push_back(allpe(gw, bob["USD"]));
                result.push_back(cpe(EUR.currency));
                return result;
            }();

            PathSet paths(p);

            env(pay(alice, alice, EUR(1)),
                json(paths.json()),
                sendmax(XRP(10)),
                txflags(tfNoRippleDirect | tfPartialPayment),
                ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), XRP(100)), txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), XRP(100)), txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                path(~USD, ~XRP),
                sendmax(XRP(200)),
                txflags(tfNoRippleDirect),
                ter(temBAD_SEND_XRP_MAX));
        }
    }

    void
    testLoop(FeatureBitset features)
    {
        testcase("test loop");
        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];
        auto const CNY = gw["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));
            env(pay(gw, alice, USD(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), XRP(100)), txflags(tfPassive));

            // payment path: USD -> USD/XRP -> XRP/USD
            env(pay(alice, carol, USD(100)),
                sendmax(USD(100)),
                path(~XRP, ~USD),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(USD(10000), alice, bob, carol);
            env.trust(EUR(10000), alice, bob, carol);
            env.trust(CNY(10000), alice, bob, carol);

            env(pay(gw, bob, USD(100)));
            env(pay(gw, bob, EUR(100)));
            env(pay(gw, bob, CNY(100)));

            env(offer(bob, XRP(100), USD(100)), txflags(tfPassive));
            env(offer(bob, USD(100), EUR(100)), txflags(tfPassive));
            env(offer(bob, EUR(100), CNY(100)), txflags(tfPassive));

            // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
            env(pay(alice, carol, CNY(100)),
                sendmax(XRP(100)),
                path(~USD, ~EUR, ~USD, ~CNY),
                txflags(tfNoRippleDirect),
                ter(temBAD_PATH_LOOP));
        }
    }

    void
    testNoAccount(FeatureBitset features)
    {
        testcase("test no account");
        using namespace jtx;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");
        auto const USD = gw["USD"];

        Env env(*this, features);
        env.fund(XRP(10000), alice, bob, gw);

        STAmount sendMax{USD.issue(), 100, 1};
        STAmount noAccountAmount{Issue{USD.currency, noAccount()}, 100, 1};
        STAmount deliver;
        AccountID const srcAcc = alice.id();
        AccountID dstAcc = bob.id();
        STPathSet pathSet;
        ::ripple::path::RippleCalc::Input inputs;
        inputs.defaultPathsAllowed = true;
        try
        {
            PaymentSandbox sb{env.current().get(), tapNONE};
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    dstAcc,
                    noAccount(),
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    noAccount(),
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    noAccountAmount,
                    deliver,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::ripple::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    noAccountAmount,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app().logs(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
        }
        catch (...)
        {
            this->fail();
        }
    }

    void
    run() override
    {
        using namespace jtx;
        auto const sa = testable_amendments();
        testToStrand(sa - featurePermissionedDEX);
        testToStrand(sa);

        testRIPD1373(sa - featurePermissionedDEX);
        testRIPD1373(sa);

        testLoop(sa - featurePermissionedDEX);
        testLoop(sa);

        testNoAccount(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PayStrand, app, ripple);

}  // namespace test
}  // namespace ripple
