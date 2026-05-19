#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/PathSet.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/balance.h>
#include <test/jtx/flags.h>
#include <test/jtx/jtx_json.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ter.h>
#include <test/jtx/trust.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/safe_cast.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/PaymentSandbox.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Book.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STPathSet.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/tx/paths/RippleCalc.h>
#include <xrpl/tx/paths/detail/Steps.h>
#include <xrpl/tx/transactors/dex/AMMContext.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace xrpl::test {

enum class TrustFlag { Freeze, Auth, Noripple };

/*constexpr*/ std::uint32_t
trustFlag(TrustFlag f, bool useHigh)
{
    switch (f)
    {
        case TrustFlag::Freeze:
            if (useHigh)
                return lsfHighFreeze;
            return lsfLowFreeze;
        case TrustFlag::Auth:
            if (useHigh)
                return lsfHighAuth;
            return lsfLowAuth;
        case TrustFlag::Noripple:
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

class ElementComboIter
{
    enum class SB /*state bit*/
        : std::uint16_t {
            Acc,
            Iss,
            Cur,
            RootAcc,
            RootIss,
            Xrp,
            SameAccIss,
            ExistingAcc,
            ExistingCur,
            ExistingIss,
            PrevAcc,
            PrevCur,
            PrevIss,
            Boundary,
            Last
        };

    std::uint16_t state_ = 0;
    static_assert(safeCast<size_t>(SB::Last) <= sizeof(decltype(state_)) * 8, "");
    STPathElement const* prev_ = nullptr;
    // disallow iss and cur to be specified with acc is specified (simplifies
    // some tests)
    bool const allowCompound_ = false;

    [[nodiscard]] bool
    has(SB s) const
    {
        return (state_ & (1 << safeCast<int>(s))) != 0;
    }

    [[nodiscard]] bool
    hasAny(std::initializer_list<SB> sb) const
    {
        for (auto const s : sb)
        {
            if (has(s))
                return true;
        }
        return false;
    }

    [[nodiscard]] size_t
    count(std::initializer_list<SB> sb) const
    {
        size_t result = 0;

        for (auto const s : sb)
        {
            if (has(s))
                result++;
        }
        return result;
    }

public:
    explicit ElementComboIter(STPathElement const* prev = nullptr) : prev_(prev)
    {
    }

    [[nodiscard]] bool
    valid() const
    {
        return (allowCompound_ || !(has(SB::Acc) && hasAny({SB::Cur, SB::Iss}))) &&
            (!hasAny({SB::PrevAcc, SB::PrevCur, SB::PrevIss}) || (prev_ != nullptr)) &&
            (!hasAny({SB::RootAcc, SB::SameAccIss, SB::ExistingAcc, SB::PrevAcc}) ||
             has(SB::Acc)) &&
            (!hasAny({SB::RootIss, SB::SameAccIss, SB::ExistingIss, SB::PrevIss}) ||
             has(SB::Iss)) &&
            (!hasAny({SB::Xrp, SB::ExistingCur, SB::PrevCur}) || has(SB::Cur)) &&
            // These will be duplicates
            (count({SB::Xrp, SB::ExistingCur, SB::PrevCur}) <= 1) &&
            (count({SB::RootAcc, SB::ExistingAcc, SB::PrevAcc}) <= 1) &&
            (count({SB::RootIss, SB::ExistingIss, SB::RootIss}) <= 1);
    }
    bool
    next()
    {
        if (!(has(SB::Last)))
        {
            do
            {
                ++state_;
            } while (!valid());
        }
        return !has(SB::Last);
    }

    template <class Col, class AccFactory, class IssFactory, class CurrencyFactory>
    void
    emplaceInto(
        Col& col,
        AccFactory&& accF,
        IssFactory&& issF,
        CurrencyFactory&& currencyF,
        std::optional<AccountID> const& existingAcc,
        std::optional<Currency> const& existingCur,
        std::optional<AccountID> const& existingIss)
    {
        assert(!has(SB::Last));

        auto const acc = [&]() -> std::optional<AccountID> {
            if (!has(SB::Acc))
                return std::nullopt;
            if (has(SB::RootAcc))
                return xrpAccount();
            if (has(SB::ExistingAcc) && existingAcc)
                return existingAcc;
            return accF().id();
        }();
        auto const iss = [&]() -> std::optional<AccountID> {
            if (!has(SB::Iss))
                return std::nullopt;
            if (has(SB::RootIss))
                return xrpAccount();
            if (has(SB::SameAccIss))
                return acc;
            if (has(SB::ExistingIss) && existingIss)
                return existingIss;
            return issF().id();
        }();
        auto const cur = [&]() -> std::optional<Currency> {
            if (!has(SB::Cur))
                return std::nullopt;
            if (has(SB::Xrp))
                return xrpCurrency();
            if (has(SB::ExistingCur) && existingCur)
                return existingCur;
            return currencyF();
        }();
        if (!has(SB::Boundary))
        {
            col.emplace_back(acc, cur, iss);
        }
        else
        {
            col.emplace_back(
                STPathElement::Type::TypeBoundary,
                acc.value_or(AccountID{}),
                cur.value_or(Currency{}),
                iss.value_or(AccountID{}));
        }
    }
};

struct ExistingElementPool
{
    std::vector<jtx::Account> accounts;
    std::vector<xrpl::Currency> currencies;
    std::vector<std::string> currencyNames;

    jtx::Account
    getAccount(size_t id)
    {
        assert(id < accounts.size());
        return accounts[id];
    }

    xrpl::Currency
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
    [[nodiscard]] ResetState
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
        ExistingElementPool& p;
        ResetState state;

        explicit StateGuard(ExistingElementPool& p) : p{p}, state{p.getResetState()}
        {
        }
        ~StateGuard()
        {
            p.resetTo(state);
        }
    };

    // Create the given number of accounts, and add trust lines so every
    // account trusts every other with every currency
    // Create an offer from every currency/account to every other
    // currency/account; the offer owner is either the specified
    // account or the issuer of the "taker gets" account
    void
    setupEnv(jtx::Env& env, size_t numAct, size_t numCur, std::optional<size_t> const& offererIndex)
    {
        using namespace jtx;

        assert(!offererIndex || offererIndex < numAct);

        accounts.clear();
        accounts.reserve(numAct);
        currencies.clear();
        currencies.reserve(numCur);
        currencyNames.clear();
        currencyNames.reserve(numCur);

        static constexpr size_t kBufSize = 32;
        char buf[kBufSize];

        for (size_t id = 0; id < numAct; ++id)
        {
            snprintf(buf, kBufSize, "A%zu", id);
            accounts.emplace_back(buf);
        }

        for (size_t id = 0; id < numCur; ++id)
        {
            if (id < 10)
            {
                snprintf(buf, kBufSize, "CC%zu", id);
            }
            else if (id < 100)
            {
                snprintf(buf, kBufSize, "C%zu", id);
            }
            else
            {
                snprintf(buf, kBufSize, "%zu", id);
            }
            currencies.emplace_back(toCurrency(buf));
            currencyNames.emplace_back(buf);
        }

        for (auto const& a : accounts)
            env.fund(XRP(100000), a);

        // Every account trusts every other account with every currency
        for (auto ai1 = accounts.begin(), aie = accounts.end(); ai1 != aie; ++ai1)
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
        {
            for (auto const& cn : currencyNames)
                ious.emplace_back(a[cn]);
        }

        // create offers from every currency to every other currency
        for (auto takerPays = ious.begin(), ie = ious.end(); takerPays != ie; ++takerPays)
        {
            for (auto takerGets = ious.begin(); takerGets != ie; ++takerGets)
            {
                if (takerPays == takerGets)
                    continue;
                auto const owner = offererIndex ? accounts[*offererIndex] : takerGets->account;
                if (owner.id() != takerGets->account.id())
                    env(pay(takerGets->account, owner, (*takerGets)(1000)));

                env(offer(owner, (*takerPays)(1000), (*takerGets)(1000)), Txflags(tfPassive));
            }
            env.close();
        }

        // create offers to/from xrp to every other ious
        for (auto const& iou : ious)
        {
            auto const owner = offererIndex ? accounts[*offererIndex] : iou.account;
            env(offer(owner, iou(1000), XRP(1000)), Txflags(tfPassive));
            env(offer(owner, XRP(1000), iou(1000)), Txflags(tfPassive));
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

        auto xrpBalance = [](ReadView const& v, xrpl::Keylet const& k) {
            auto const sle = v.read(k);
            if (!sle)
                return STAmount{};
            return (*sle)[sfBalance];
        };
        auto lineBalance = [](ReadView const& v, xrpl::Keylet const& k) {
            auto const sle = v.read(k);
            if (!sle)
                return STAmount{};
            return (*sle)[sfBalance];
        };
        std::uint64_t totalXRP[2]{};
        for (auto ai1 = accounts.begin(), aie = accounts.end(); ai1 != aie; ++ai1)
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

    xrpl::Currency
    getAvailCurrency()
    {
        return getCurrency(nextAvailCurrency++);
    }

    template <class F>
    void
    forEachElementPair(
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

        STPathElement const* prevOuter = prefix.empty() ? nullptr : &prefix.back();
        ElementComboIter outer(prevOuter);

        std::vector<STPathElement> outerResult;
        std::vector<STPathElement> result;
        auto const resultSize = prefix.size() + suffix.size() + 2;
        outerResult.reserve(resultSize);
        result.reserve(resultSize);
        while (outer.next())
        {
            StateGuard const og{*this};
            outerResult = prefix;
            outer.emplaceInto(
                outerResult, accF, issF, currencyF, existingAcc, existingCur, existingIss);
            STPathElement const* prevInner = &outerResult.back();
            ElementComboIter inner(prevInner);
            while (inner.next())
            {
                StateGuard const ig{*this};
                result = outerResult;
                inner.emplaceInto(
                    result, accF, issF, currencyF, existingAcc, existingCur, existingIss);
                result.insert(result.end(), suffix.begin(), suffix.end());
                f(sendMax, deliver, result);
            }
        };
    }
};

struct PayStrand_test : public beast::unit_test::Suite
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

        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        auto const eurC = eur.currency;
        auto const usdC = usd.currency;

        using D = DirectStepInfo;
        using B = xrpl::Book;
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
                OfferCrossing::No,
                ammContext,
                std::nullopt,
                env.app().getJournal("Flow"));
            BEAST_EXPECT(ter == expTer);
            if (sizeof...(expSteps) != 0)
                BEAST_EXPECT(equal(strand, std::forward<decltype(expSteps)>(expSteps)...));
        };

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(1000), alice, bob);
            env.trust(eur(1000), alice, bob);
            env(pay(gw, alice, eur(100)));

            {
                STPath const path = STPath({ipe(bob["USD"]), cpe(eur.currency)});
                auto [ter, _] = toStrand(
                    *env.current(),
                    alice,
                    alice,
                    /*deliver*/ xrpIssue(),
                    /*limitQuality*/ std::nullopt,
                    /*sendMaxIssue*/ eur,
                    path,
                    true,
                    OfferCrossing::No,
                    ammContext,
                    std::nullopt,
                    env.app().getJournal("Flow"));
                (void)_;
                BEAST_EXPECT(isTesSuccess(ter));
            }
            {
                STPath const path = STPath({ipe(usd), cpe(xrpCurrency())});
                auto [ter, _] = toStrand(
                    *env.current(),
                    alice,
                    alice,
                    /*deliver*/ xrpIssue(),
                    /*limitQuality*/ std::nullopt,
                    /*sendMaxIssue*/ eur,
                    path,
                    true,
                    OfferCrossing::No,
                    ammContext,
                    std::nullopt,
                    env.app().getJournal("Flow"));
                (void)_;
                BEAST_EXPECT(isTesSuccess(ter));
            }
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, carol, gw);

            test(env, usd, std::nullopt, STPath(), terNO_LINE);

            env.trust(usd(1000), alice, bob, carol);
            test(env, usd, std::nullopt, STPath(), tecPATH_DRY);

            env(pay(gw, alice, usd(100)));
            env(pay(gw, carol, usd(100)));

            // Insert implied account
            test(
                env, usd, std::nullopt, STPath(), tesSUCCESS, D{alice, gw, usdC}, D{gw, bob, usdC});
            env.trust(eur(1000), alice, bob);

            // Insert implied offer
            test(
                env,
                eur,
                usd,
                STPath(),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{usd, eur, std::nullopt},
                D{gw, bob, eurC});

            // Path with explicit offer
            test(
                env,
                eur,
                usd,
                STPath({ipe(eur)}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{usd, eur, std::nullopt},
                D{gw, bob, eurC});

            // Path with offer that changes issuer only
            env.trust(carol["USD"](1000), bob);
            test(
                env,
                carol["USD"],
                usd,
                STPath({iape(carol)}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{usd, carol["USD"], std::nullopt},
                D{carol, bob, usdC});

            // Path with XRP src currency
            test(
                env,
                usd,
                xrpIssue(),
                STPath({ipe(usd)}),
                tesSUCCESS,
                XRPS{alice},
                B{XRP, usd, std::nullopt},
                D{gw, bob, usdC});

            // Path with XRP dst currency.
            test(
                env,
                xrpIssue(),
                usd,
                STPath({STPathElement{
                    STPathElement::TypeCurrency, xrpAccount(), xrpCurrency(), xrpAccount()}}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{usd, XRP, std::nullopt},
                XRPS{bob});

            // Path with XRP cross currency bridged payment
            test(
                env,
                eur,
                usd,
                STPath({cpe(xrpCurrency())}),
                tesSUCCESS,
                D{alice, gw, usdC},
                B{usd, XRP, std::nullopt},
                B{XRP, eur, std::nullopt},
                D{gw, bob, eurC});

            // XRP -> XRP transaction can't include a path
            test(env, XRP, std::nullopt, STPath({ape(carol)}), temBAD_PATH);

            {
                // The root account can't be the src or dst
                auto flowJournal = env.app().getJournal("Flow");
                {
                    // The root account can't be the dst
                    auto r = toStrand(
                        *env.current(),
                        alice,
                        xrpAccount(),
                        XRP,
                        std::nullopt,
                        usd,
                        STPath(),
                        true,
                        OfferCrossing::No,
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
                        OfferCrossing::No,
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
                        usd,
                        std::nullopt,
                        std::nullopt,
                        STPath(),
                        true,
                        OfferCrossing::No,
                        ammContext,
                        std::nullopt,
                        flowJournal);
                    BEAST_EXPECT(r.first == temBAD_PATH);
                }
            }

            // Create an offer with the same in/out issue
            test(env, eur, usd, STPath({ipe(usd), ipe(eur)}), temBAD_PATH);

            // Path element with type zero
            test(
                env,
                usd,
                std::nullopt,
                STPath({STPathElement(0, xrpAccount(), xrpCurrency(), xrpAccount())}),
                temBAD_PATH);

            // The same account can't appear more than once on a path
            // `gw` will be used from alice->carol and implied between carol
            // and bob
            test(env, usd, std::nullopt, STPath({ape(gw), ape(carol)}), temBAD_PATH_LOOP);

            // The same offer can't appear more than once on a path
            test(env, eur, usd, STPath({ipe(eur), ipe(usd), ipe(eur)}), temBAD_PATH_LOOP);
        }

        {
            // cannot have more than one offer with the same output issue

            using namespace jtx;
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(usd(10000), alice, bob, carol);
            env.trust(eur(10000), alice, bob, carol);

            env(pay(gw, bob, usd(100)));
            env(pay(gw, bob, eur(100)));

            env(offer(bob, XRP(100), usd(100)));
            env(offer(bob, usd(100), eur(100)), Txflags(tfPassive));
            env(offer(bob, eur(100), usd(100)), Txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/EUR -> EUR/USD
            env(pay(alice, carol, usd(100)),
                Path(~usd, ~eur, ~usd),
                Sendmax(XRP(200)),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, noripple(gw));
            env.trust(usd(1000), alice, bob);
            env(pay(gw, alice, usd(100)));
            test(env, usd, std::nullopt, STPath(), terNO_RIPPLE);
        }

        {
            // check global freeze
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(1000), alice, bob);
            env(pay(gw, alice, usd(100)));

            // Account can still issue payments
            env(fset(alice, asfGlobalFreeze));
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);
            env(fclear(alice, asfGlobalFreeze));
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);

            // Account can not issue funds
            env(fset(gw, asfGlobalFreeze));
            test(env, usd, std::nullopt, STPath(), terNO_LINE);
            env(fclear(gw, asfGlobalFreeze));
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);

            // Account can not receive funds
            env(fset(bob, asfGlobalFreeze));
            test(env, usd, std::nullopt, STPath(), terNO_LINE);
            env(fclear(bob, asfGlobalFreeze));
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);
        }
        {
            // Freeze between gw and alice
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(1000), alice, bob);
            env(pay(gw, alice, usd(100)));
            test(env, usd, std::nullopt, STPath(), tesSUCCESS);
            env(trust(gw, alice["USD"](0), tfSetFreeze));
            BEAST_EXPECT(getTrustFlag(env, gw, alice, usdC, TrustFlag::Freeze));
            test(env, usd, std::nullopt, STPath(), terNO_LINE);
        }
        {
            // check no auth
            // An account may require authorization to receive IOUs from an
            // issuer
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfRequireAuth));
            env.trust(usd(1000), alice, bob);
            // Authorize alice but not bob
            env(trust(gw, alice["USD"](1000), tfSetfAuth));
            BEAST_EXPECT(getTrustFlag(env, gw, alice, usdC, TrustFlag::Auth));
            env(pay(gw, alice, usd(100)));
            env.require(Balance(alice, usd(100)));
            test(env, usd, std::nullopt, STPath(), terNO_AUTH);

            // Check pure issue redeem still works
            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                gw,
                usd,
                std::nullopt,
                std::nullopt,
                STPath(),
                true,
                OfferCrossing::No,
                ammContext,
                std::nullopt,
                env.app().getJournal("Flow"));
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(equal(strand, D{alice, gw, usdC}));
        }

        {
            // last step xrp from offer
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.trust(usd(1000), alice, bob);
            env(pay(gw, alice, usd(100)));

            // alice -> USD/XRP -> bob
            STPath path;
            path.emplaceBack(std::nullopt, xrpCurrency(), std::nullopt);

            auto [ter, strand] = toStrand(
                *env.current(),
                alice,
                bob,
                XRP,
                std::nullopt,
                usd,
                path,
                false,
                OfferCrossing::No,
                ammContext,
                std::nullopt,
                env.app().getJournal("Flow"));
            BEAST_EXPECT(isTesSuccess(ter));
            BEAST_EXPECT(
                equal(strand, D{alice, gw, usdC}, B{usd, xrpIssue(), std::nullopt}, XRPS{bob}));
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
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];

        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);

            env.trust(usd(1000), alice, bob);
            env.trust(eur(1000), alice, bob);
            env.trust(bob["USD"](1000), alice, gw);
            env.trust(bob["EUR"](1000), alice, gw);

            env(offer(bob, XRP(100), bob["USD"](100)), Txflags(tfPassive));
            env(offer(gw, XRP(100), usd(100)), Txflags(tfPassive));

            env(offer(bob, bob["USD"](100), bob["EUR"](100)), Txflags(tfPassive));
            env(offer(gw, usd(100), eur(100)), Txflags(tfPassive));

            TestPath const p = [&] {
                TestPath result;
                result.pushBack(allPathElements(gw, bob["USD"]));
                result.pushBack(cpe(eur.currency));
                return result;
            }();

            PathSet const paths(p);

            env(pay(alice, alice, eur(1)),
                Json(paths.json()),
                Sendmax(XRP(10)),
                Txflags(tfNoRippleDirect | tfPartialPayment),
                Ter(temBAD_PATH));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(usd(10000), alice, bob, carol);

            env(pay(gw, bob, usd(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), XRP(100)), Txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                Path(~usd, ~XRP),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_SEND_XRP_PATHS));
        }

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(usd(10000), alice, bob, carol);

            env(pay(gw, bob, usd(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), XRP(100)), Txflags(tfPassive));

            // payment path: XRP -> XRP/USD -> USD/XRP
            env(pay(alice, carol, XRP(100)),
                Path(~usd, ~XRP),
                Sendmax(XRP(200)),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_SEND_XRP_MAX));
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
        auto const usd = gw["USD"];
        auto const eur = gw["EUR"];
        auto const cny = gw["CNY"];

        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(usd(10000), alice, bob, carol);

            env(pay(gw, bob, usd(100)));
            env(pay(gw, alice, usd(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), XRP(100)), Txflags(tfPassive));

            // payment path: USD -> USD/XRP -> XRP/USD
            env(pay(alice, carol, usd(100)),
                Sendmax(usd(100)),
                Path(~XRP, ~usd),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
        }
        {
            Env env(*this, features);

            env.fund(XRP(10000), alice, bob, carol, gw);
            env.trust(usd(10000), alice, bob, carol);
            env.trust(eur(10000), alice, bob, carol);
            env.trust(cny(10000), alice, bob, carol);

            env(pay(gw, bob, usd(100)));
            env(pay(gw, bob, eur(100)));
            env(pay(gw, bob, cny(100)));

            env(offer(bob, XRP(100), usd(100)), Txflags(tfPassive));
            env(offer(bob, usd(100), eur(100)), Txflags(tfPassive));
            env(offer(bob, eur(100), cny(100)), Txflags(tfPassive));

            // payment path: XRP->XRP/USD->USD/EUR->USD/CNY
            env(pay(alice, carol, cny(100)),
                Sendmax(XRP(100)),
                Path(~usd, ~eur, ~usd, ~cny),
                Txflags(tfNoRippleDirect),
                Ter(temBAD_PATH_LOOP));
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
        auto const usd = gw["USD"];

        Env env(*this, features);
        env.fund(XRP(10000), alice, bob, gw);

        STAmount const sendMax{usd, 100, 1};
        STAmount const noAccountAmount{Issue{usd.currency, noAccount()}, 100, 1};
        STAmount const deliver;
        AccountID const srcAcc = alice.id();
        AccountID const dstAcc = bob.id();
        STPathSet const pathSet;
        ::xrpl::path::RippleCalc::Input inputs;
        inputs.defaultPathsAllowed = true;
        try
        {
            PaymentSandbox sb{env.current().get(), TapNone};
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    dstAcc,
                    noAccount(),
                    pathSet,
                    std::nullopt,
                    env.app(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    deliver,
                    noAccount(),
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    noAccountAmount,
                    deliver,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app(),
                    &inputs);
                BEAST_EXPECT(r.result() == temBAD_PATH);
            }
            {
                auto const r = ::xrpl::path::RippleCalc::rippleCalculate(
                    sb,
                    sendMax,
                    noAccountAmount,
                    dstAcc,
                    srcAcc,
                    pathSet,
                    std::nullopt,
                    env.app(),
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
        auto const sa = testableAmendments();
        testToStrand(sa - featurePermissionedDEX);
        testToStrand(sa);

        testRIPD1373(sa - featurePermissionedDEX);
        testRIPD1373(sa);

        testLoop(sa - featurePermissionedDEX);
        testLoop(sa);

        testNoAccount(sa);
    }
};

BEAST_DEFINE_TESTSUITE(PayStrand, app, xrpl);

}  // namespace xrpl::test
