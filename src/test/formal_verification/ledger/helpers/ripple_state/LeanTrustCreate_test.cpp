#include <test/formal_verification/ffi/ledger/helpers/RippleStateHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/RippleStateHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <string>

namespace xrpl::test {

using namespace formal_verification;

class LeanTrustCreate_test : public LedgerSuite
{
    void
    runTrustCreate(
        Sandbox& sb,
        bool bSrcHigh,
        AccountID const& src,
        AccountID const& dst,
        AccountID const& setAccount,
        bool bAuth,
        bool bNoRipple,
        bool bFreeze,
        bool bDeepFreeze,
        STAmount const& saBalance,
        STAmount const& saLimit,
        uint32_t uQualityIn,
        uint32_t uQualityOut,
        TER expected,
        char const* label)
    {
        uint256 const uIndex = keylet::line(src, dst, saBalance.get<Issue>().currency).key;
        auto sle = sb.peek(keylet::account(setAccount));
        std::optional<AccountRootFFI> acctFFI;
        if (sle)
            acctFFI =
                AccountRootFFIBuilder().fromCpp(ledger_entries::AccountRoot(sle)).build(sle->key());
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = trustCreate(
                sb,
                bSrcHigh,
                src,
                dst,
                uIndex,
                sle,
                bAuth,
                bNoRipple,
                bFreeze,
                bDeepFreeze,
                saBalance,
                saLimit,
                uQualityIn,
                uQualityOut,
                beast::Journal{beast::Journal::getNullSink()});
            LeanTerResult const leanRes = formal_verification::trustCreate(
                ledger,
                bSrcHigh,
                src,
                dst,
                uIndex,
                acctFFI ? &*acctFFI : nullptr,
                bAuth,
                bNoRipple,
                bFreeze,
                bDeepFreeze,
                saBalance,
                saLimit,
                uQualityIn,
                uQualityOut);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    static STAmount
    balance(Currency const& usd, std::int64_t v)
    {
        return STAmount{Issue{usd, noAccount()}, v};
    }

    static STAmount
    limitFor(Currency const& usd, AccountID const& acct, std::int64_t v)
    {
        return STAmount{Issue{usd, acct}, v};
    }

    // Early guards: self-line, null set-account, and absent peer.
    void
    testTrustCreateGuards()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const ghost("ghost");  // never created
        Env env(*this);
        env.fund(XRP(1000), alice, bob);
        env.close();
        Currency const usd = toCurrency("USD");

        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                false,
                alice.id(),
                alice.id(),
                alice.id(),
                false,
                false,
                false,
                false,
                balance(usd, 50),
                limitFor(usd, alice.id(), 100),
                0,
                0,
                tecINTERNAL,
                "trustCreate.self");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                false,
                alice.id(),
                bob.id(),
                ghost.id(),
                false,
                false,
                false,
                false,
                balance(usd, 50),
                limitFor(usd, alice.id(), 100),
                0,
                0,
                tefINTERNAL,
                "trustCreate.set_account_null");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            bool const srcHigh = alice.id() > ghost.id();
            runTrustCreate(
                sb,
                srcHigh,
                alice.id(),
                ghost.id(),
                alice.id(),
                false,
                false,
                false,
                false,
                balance(usd, 50),
                limitFor(usd, alice.id(), 100),
                0,
                0,
                tecNO_TARGET,
                "trustCreate.peer_absent");
        }
    }

    // Field/flag selection by src-high vs src-low, and the dst-as-set-account path.
    void
    testTrustCreateFieldSelection()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");  // no DefaultRipple
        Env env(*this);
        env.fund(XRP(1000), alice, bob);
        env.fund(XRP(1000), noripple(carol));
        env.close();
        Currency const usd = toCurrency("USD");
        AccountID const low = std::min(alice.id(), bob.id());
        AccountID const high = std::max(alice.id(), bob.id());

        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                false,
                low,
                high,
                low,
                true,
                false,
                false,
                false,
                balance(usd, 50),
                limitFor(usd, low, 100),
                7,
                9,
                tesSUCCESS,
                "trustCreate.src_low");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                true,
                high,
                low,
                high,
                false,
                false,
                true,
                true,
                balance(usd, -50),
                limitFor(usd, high, 100),
                0,
                0,
                tesSUCCESS,
                "trustCreate.src_high");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                false,
                low,
                high,
                high,
                false,
                true,
                false,
                false,
                balance(usd, 50),
                limitFor(usd, high, 200),
                0,
                0,
                tesSUCCESS,
                "trustCreate.set_dst");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            bool const srcHigh = alice.id() > carol.id();
            runTrustCreate(
                sb,
                srcHigh,
                alice.id(),
                carol.id(),
                alice.id(),
                false,
                false,
                false,
                false,
                balance(usd, 50),
                limitFor(usd, alice.id(), 100),
                0,
                0,
                tesSUCCESS,
                "trustCreate.peer_no_default_ripple");
        }
    }

    // Balance/limit/quality staged at the int64 / uint32 storage bounds.
    void
    testTrustCreateExtremes()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Env env(*this);
        env.fund(XRP(1000), alice, bob);
        env.close();
        Currency const usd = toCurrency("USD");
        AccountID const low = std::min(alice.id(), bob.id());
        AccountID const high = std::max(alice.id(), bob.id());
        constexpr int64_t int64max = std::numeric_limits<int64_t>::max();
        constexpr int64_t int64min = std::numeric_limits<int64_t>::min();
        constexpr uint32_t u32max = std::numeric_limits<uint32_t>::max();

        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                false,
                low,
                high,
                low,
                false,
                false,
                false,
                false,
                balance(usd, int64max),
                limitFor(usd, low, int64max),
                u32max,
                u32max,
                tesSUCCESS,
                "trustCreate.extremes_max");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            runTrustCreate(
                sb,
                false,
                low,
                high,
                low,
                false,
                false,
                false,
                false,
                balance(usd, int64min),
                limitFor(usd, low, int64max),
                u32max,
                u32max,
                tesSUCCESS,
                "trustCreate.extremes_min");
        }
    }

    // An MPT-asset balance makes both sides fault before completing.
    void
    testTrustCreateThrows()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const bob("bob");
        Env env(*this);
        env.fund(XRP(1000), gw, alice, bob);
        env.close();
        Currency const usd = toCurrency("USD");

        STAmount const mptBalance{MPTIssue{makeMptID(1, gw.id())}, std::int64_t(50)};
        STAmount const saLimit{Issue{usd, alice.id()}, 100};
        uint256 const uIndex = keylet::line(alice.id(), bob.id(), usd).key;

        Sandbox sb(&*env.current(), TapNone);
        auto sle = sb.peek(keylet::account(alice.id()));
        AccountRootFFI const acctFFI =
            AccountRootFFIBuilder().fromCpp(ledger_entries::AccountRoot(sle)).build(sle->key());

        runLedgerTest(sb, "trustCreate.mpt_balance_throws", [&](LedgerFFI& ledger) {
            Sandbox sbThrow(&*env.current(), TapNone);
            auto sleThrow = sbThrow.peek(keylet::account(alice.id()));
            bool cppThrew = false;
            std::string cppError;
            try
            {
                static_cast<void>(trustCreate(
                    sbThrow,
                    false,
                    alice.id(),
                    bob.id(),
                    uIndex,
                    sleThrow,
                    false,
                    false,
                    false,
                    false,
                    mptBalance,
                    saLimit,
                    0,
                    0,
                    beast::Journal{beast::Journal::getNullSink()}));
            }
            catch (std::exception const& e)
            {
                cppThrew = true;
                cppError = e.what();
            }
            LeanTerResult const leanRes = formal_verification::trustCreate(
                ledger,
                false,
                alice.id(),
                bob.id(),
                uIndex,
                &acctFFI,
                false,
                false,
                false,
                false,
                mptBalance,
                saLimit,
                0,
                0);
            BEAST_EXPECT(cppThrew);
            BEAST_EXPECT(leanRes.threw);
            BEAST_EXPECTS(
                !cppError.empty() && cppError == leanRes.error,
                "cpp=[" + cppError + "] lean=[" + leanRes.error + "]");
        });
    }

    void
    runTests() override
    {
        testTrustCreateGuards();
        testTrustCreateFieldSelection();
        testTrustCreateExtremes();
        testTrustCreateThrows();
    }
};

BEAST_DEFINE_TESTSUITE(LeanTrustCreate, formal_verification, xrpl);

}  // namespace xrpl::test
