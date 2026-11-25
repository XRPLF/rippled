// Manual benchmark: apply cost of transactions whose work varies with input,
// relative to a plain XRP payment.
// Run: xrpld --unittest xrpl.app.TxFeeBench
//
// Each case applies one pre-signed transaction to a fresh OpenView over the
// same closed ledger, so every repetition starts from the same state. Only
// xrpl::apply (preflight + preclaim + doApply) is timed. The HashRouter caches
// the signature verdict after the first repetition, so the median excludes
// signature verification.

#include <test/jtx/AMM.h>
#include <test/jtx/Account.h>
#include <test/jtx/ConfidentialTransfer.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/mpt.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/sendmax.h>
#include <test/jtx/ticket.h>
#include <test/jtx/token.h>
#include <test/jtx/txflags.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SeqProxy.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/tx/apply.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <ostream>
#include <ratio>
#include <sstream>
#include <string>
#include <vector>

namespace xrpl::test {

class TxFeeBench_test : public ConfidentialTransferTestBase
{
    static constexpr int kMaxPaths = 6;
    static constexpr int kMaxLen = 8;
    static constexpr int kDeepOffers = 1000;
    static constexpr int kReps = 200;

    double baseline_ = 0;

    static std::string
    hopCode(int path, int hop)
    {
        return std::string{static_cast<char>('A' + path), 'H', static_cast<char>('0' + hop)};
    }

    // Median apply time in microseconds, or -1 on an unexpected result.
    double
    timeApply(jtx::Env& env, jtx::JTx const& jt, int reps, TER expected = tesSUCCESS)
    {
        auto const base = env.closed();
        beast::Journal const j{beast::Journal::getNullSink()};
        std::vector<double> samples;
        samples.reserve(reps);
        for (int i = 0; i < reps; ++i)
        {
            OpenView view(kOpenLedger, base->rules(), base);
            auto const start = std::chrono::steady_clock::now();
            auto const result = xrpl::apply(env.app(), view, *jt.stx, TapNone, j);
            auto const stop = std::chrono::steady_clock::now();
            if (!BEAST_EXPECT(result.applied && result.ter == expected))
            {
                log << "  unexpected result: " << transToken(result.ter) << std::endl;
                return -1;
            }
            samples.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        }
        std::ranges::sort(samples);
        return samples[samples.size() / 2];
    }

    void
    bench(
        std::string const& name,
        jtx::Env& env,
        jtx::JTx const& jt,
        int reps = kReps,
        TER expected = tesSUCCESS)
    {
        double const us = timeApply(env, jt, reps, expected);
        if (baseline_ == 0)
            baseline_ = us;
        std::ostringstream os;
        os << std::fixed << std::setprecision(1) << std::left << std::setw(40) << name << std::right
           << std::setw(10) << us << " us " << std::setw(8) << (us / baseline_) << "x";
        log << os.str() << std::endl;
    }

    void
    section(std::string const& title)
    {
        log << "--- " << title << std::endl;
    }

    void
    benchPayments()
    {
        using namespace jtx;
        Env env(*this);

        Account const gw{"gw"};
        Account const maker{"maker"};
        Account const ghost{"ghost"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];
        auto const unf = gw["UNF"];

        env.fund(XRP(100'000'000), gw, maker, ghost);
        env.fund(XRP(1'000'000), alice, bob);
        env.close();

        env.trust(usd(1e9), maker, alice, bob);
        env.trust(unf(1e9), maker, ghost, bob);
        for (int p = 0; p < kMaxPaths; ++p)
        {
            for (int h = 1; h <= kMaxLen; ++h)
                env.trust(gw[hopCode(p, h)](1e9), maker);
        }
        env.close();

        env(pay(gw, maker, usd(1e8)));
        env(pay(gw, alice, usd(1e6)));
        env(pay(gw, maker, unf(1e6)));
        env(pay(gw, ghost, unf(1e6)));
        for (int p = 0; p < kMaxPaths; ++p)
        {
            for (int h = 1; h <= kMaxLen; ++h)
                env(pay(gw, maker, gw[hopCode(p, h)](1e8)));
        }
        env.close();

        // Hop books for each path chain: XRP -> H1 -> H2 ... and Hj -> USD.
        // One large offer per book, all at a 1:1 rate.
        for (int p = 0; p < kMaxPaths; ++p)
        {
            env(offer(maker, XRP(1'000'000), gw[hopCode(p, 1)](1'000'000)));
            for (int h = 1; h <= kMaxLen; ++h)
            {
                auto const cur = gw[hopCode(p, h)];
                if (h < kMaxLen)
                    env(offer(maker, cur(1'000'000), gw[hopCode(p, h + 1)](1'000'000)));
                env(offer(maker, cur(1'000'000), usd(1'000'000)));
            }
            env.close();
        }

        // Deep XRP -> USD book: many small offers at the same quality.
        for (int i = 0; i < kDeepOffers; ++i)
        {
            env(offer(maker, XRP(1), usd(1)));
            if (i % 100 == 99)
                env.close();
        }

        // XRP -> UNF book: ghost's offers at the best quality, which become
        // unfunded, then one funded offer from maker at a worse quality.
        for (int i = 0; i < 500; ++i)
        {
            env(offer(ghost, XRP(1), unf(1)));
            if (i % 100 == 99)
                env.close();
        }
        env(offer(maker, XRP(2), unf(1)));
        env(pay(ghost, gw, unf(1e6)));
        env.close();

        section("Payment");
        bench("XRP -> XRP (baseline)", env, env.jt(pay(alice, bob, XRP(1))));
        bench("USD -> USD direct", env, env.jt(pay(alice, bob, usd(1))));

        // Grid: N explicit paths of L elements each, tiny amount (one flow
        // iteration, one offer touched per book). Default path disabled.
        for (int const len : {1, 4, 8})
        {
            for (int const n : {1, 3, 6})
            {
                json::Value jv = pay(alice, bob, usd(0.01));
                jv[jss::Paths] = json::Value(json::ValueType::Array);
                for (int p = 0; p < n; ++p)
                {
                    json::Value path(json::ValueType::Array);
                    for (int h = 1; h <= len; ++h)
                    {
                        json::Value elem(json::ValueType::Object);
                        elem[jss::currency] = hopCode(p, h);
                        elem[jss::issuer] = gw.human();
                        path.append(elem);
                    }
                    jv[jss::Paths].append(path);
                }
                std::ostringstream name;
                name << "paths=" << n << " len=" << len << " (" << n * len << " elems)";
                bench(name.str(), env, env.jt(jv, Sendmax(XRP(1)), Txflags(tfNoRippleDirect)));
            }
        }

        // Default path only, consuming K offers in one book.
        for (int const k : {1, 10, 100, 1000})
        {
            bench(
                "default path, " + std::to_string(k) + " offers crossed",
                env,
                env.jt(pay(alice, bob, usd(k)), Sendmax(XRP(k + 1))),
                k >= 100 ? 20 : kReps);
        }

        // Default path that must first remove 500 unfunded offers.
        bench(
            "default path, 500 unfunded removed",
            env,
            env.jt(pay(alice, bob, unf(1)), Sendmax(XRP(3))),
            20);

        section("OfferCreate");
        for (int const k : {1, 10, 100, 1000})
        {
            bench(
                "OfferCreate, " + std::to_string(k) + " offers crossed",
                env,
                env.jt(offer(alice, usd(k), XRP(k))),
                k >= 100 ? 20 : kReps);
        }
        bench(
            "OfferCreate, 500 unfunded removed",
            env,
            env.jt(offer(bob, unf(1), XRP(2)), Txflags(tfImmediateOrCancel)),
            20);
    }

    void
    benchNFTsAndTickets()
    {
        using namespace jtx;
        Env env(*this);

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        env.fund(XRP(1'000'000), alice, bob, carol);
        env.close();

        auto const nftID = token::getNextID(env, alice, 0, tfTransferable);
        env(token::mint(alice, 0), Txflags(tfTransferable));
        env.close();

        std::vector<uint256> offers;
        for (int i = 0; i < 500; ++i)
        {
            offers.push_back(
                keylet::nftokenOffer(alice.id(), SeqProxy::rawSequence(env.seq(alice))).key);
            env(token::createOffer(alice, nftID, XRP(i + 1)), Txflags(tfSellNFToken));
            if (i % 100 == 99)
                env.close();
        }

        // carol fills exactly one NFT page (32 tokens), so her next mint splits it.
        for (int i = 0; i < 32; ++i)
            env(token::mint(carol, 0));
        env.close();

        section("NFT");
        bench("NFTokenMint (new page)", env, env.jt(token::mint(bob, 0)));
        bench("NFTokenMint (page split)", env, env.jt(token::mint(carol, 0)));
        bench("NFTokenAcceptOffer (sell)", env, env.jt(token::acceptSellOffer(bob, offers[0])));
        for (std::size_t const n : {1, 50, 500})
        {
            std::vector<uint256> const ids(offers.begin(), offers.begin() + n);
            bench(
                "NFTokenCancelOffer, " + std::to_string(n) + " offers",
                env,
                env.jt(token::cancelOffer(alice, ids)),
                n >= 50 ? 50 : kReps);
        }

        section("TicketCreate");
        for (std::uint32_t n : {1, 10, 250})
        {
            bench(
                "TicketCreate, " + std::to_string(n) + " tickets",
                env,
                env.jt(ticket::create(bob, n)),
                n >= 250 ? 50 : kReps);
        }
    }

    void
    benchAMM()
    {
        using namespace jtx;
        Env env(*this);

        Account const gw{"gw"};
        Account const alice{"alice"};
        Account const bob{"bob"};
        auto const usd = gw["USD"];
        env.fund(XRP(1'000'000), gw, alice, bob);
        env.close();
        env.trust(usd(1e9), alice, bob);
        env.close();
        env(pay(gw, alice, usd(100'000)));
        env.close();

        AMM const amm(env, gw, XRP(10'000), usd(10'000));

        auto const ammJv = [&](char const* type, STAmount const& amount) {
            json::Value jv;
            jv[jss::TransactionType] = type;
            jv[jss::Account] = alice.human();
            jv[jss::Asset] = toJson(xrpIssue());
            jv[jss::Asset2] = toJson(usd.issue());
            jv[jss::Amount] = amount.getJson();
            jv[jss::Flags] = static_cast<std::uint32_t>(tfSingleAsset);
            return jv;
        };

        // alice holds LP tokens so she can withdraw.
        env(ammJv("AMMDeposit", XRP(1'000)));
        env.close();

        section("AMM");
        bench(
            "Payment XRP -> USD through AMM",
            env,
            env.jt(pay(alice, bob, usd(10)), Sendmax(XRP(20))));
        bench("AMMDeposit single asset", env, env.jt(ammJv("AMMDeposit", XRP(100))));
        bench("AMMWithdraw single asset", env, env.jt(ammJv("AMMWithdraw", XRP(100))));
    }

    void
    benchConfidential()
    {
        using namespace jtx;
        Env env(*this);

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dave{"dave"};

        MPTTester mpt(env, alice, {.holders = {bob, carol, dave}});
        setupBatchEnv(mpt, alice, bob, carol, dave, 100, 50);
        env.close();

        section("Confidential MPT (fee is 10x base today)");
        bench(
            "ConfidentialMPTSend",
            env,
            env.jt(mpt.sendJV({.account = bob, .dest = carol, .amt = 10}, env.seq(bob))),
            50);
        bench("ConfidentialMPTMergeInbox", env, env.jt(mpt.mergeInboxJV({.account = carol})), 50);
    }

public:
    void
    run() override
    {
        benchPayments();
        benchNFTsAndTickets();
        benchAMM();
        benchConfidential();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(TxFeeBench, app, xrpl);

}  // namespace xrpl::test
