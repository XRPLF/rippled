#include <test/formal_verification/ffi/ledger/helpers/TokenHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/credentials.h>
#include <test/jtx/mpt.h>
#include <test/jtx/permissioned_domains.h>
#include <test/jtx/rate.h>
#include <test/jtx/vault.h>

#include <xrpl/basics/Number.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/TokenHelpers.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/MPTIssue.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/XRPAmount.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <vector>

namespace xrpl::test {

using namespace formal_verification;

class LeanAccountHolds_test : public LedgerSuite
{
    void
    runAccountHolds(
        ReadView const& view,
        AccountID const& account,
        Asset const& asset,
        FreezeHandling zeroIfFrozen,
        AuthHandling zeroIfUnauthorized,
        SpendableHandling includeFullBalance,
        STAmount const& expected,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            STAmount const cppRes = accountHolds(
                view,
                account,
                asset,
                zeroIfFrozen,
                zeroIfUnauthorized,
                beast::Journal{beast::Journal::getNullSink()},
                includeFullBalance);
            // to_nearest matches the default STAmount arithmetic C++ uses
            LeanSTAmountResult const leanRes = formal_verification::accountHolds(
                ledger,
                account,
                asset,
                static_cast<uint8_t>(zeroIfFrozen),
                static_cast<uint8_t>(zeroIfUnauthorized),
                0,
                static_cast<uint8_t>(includeFullBalance));
            BEAST_EXPECT(cppRes == expected);
            BEAST_EXPECT(cppRes.getIssuer() == expected.getIssuer());
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppRes);
            BEAST_EXPECT(leanRes.value.getIssuer() == cppRes.getIssuer());
        });
    }

    void
    runAccountHoldsThrows(
        ReadView const& view,
        AccountID const& account,
        Asset const& asset,
        FreezeHandling zeroIfFrozen,
        AuthHandling zeroIfUnauthorized,
        SpendableHandling includeFullBalance,
        char const* label)
    {
        runLedgerTest(view, label, [&](LedgerFFI const& ledger) {
            bool cppThrew = false;
            std::string cppError;
            try
            {
                (void)accountHolds(
                    view,
                    account,
                    asset,
                    zeroIfFrozen,
                    zeroIfUnauthorized,
                    beast::Journal{beast::Journal::getNullSink()},
                    includeFullBalance);
            }
            catch (std::exception const& e)
            {
                cppThrew = true;
                cppError = e.what();
            }
            LeanSTAmountResult const leanRes = formal_verification::accountHolds(
                ledger,
                account,
                asset,
                static_cast<uint8_t>(zeroIfFrozen),
                static_cast<uint8_t>(zeroIfUnauthorized),
                0,
                static_cast<uint8_t>(includeFullBalance));
            BEAST_EXPECT(cppThrew);
            BEAST_EXPECT(leanRes.threw);
            BEAST_EXPECTS(
                !cppError.empty() && cppError == leanRes.error,
                "cpp=[" + cppError + "] lean=[" + leanRes.error + "]");
        });
    }

    void
    testAccountHoldsXRP()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const ghost("ghost");  // never funded
        Env env(*this);
        env.fund(XRP(1000), alice);
        env.close();
        XRPAmount const base = env.current()->fees().accountReserve(0);

        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountHolds(
                sb,
                ghost.id(),
                Asset{xrpIssue()},
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::SimpleBalance,
                STAmount{XRPAmount{0}},
                "accountHolds.xrp_absent");
        }
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(alice.id()));
            sle->setFieldAmount(sfBalance, STAmount{base + XRPAmount{500}});
            sle->setFieldU32(sfOwnerCount, 0);
            sb.update(sle);
            runAccountHolds(
                sb,
                alice.id(),
                Asset{xrpIssue()},
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::SimpleBalance,
                STAmount{XRPAmount{500}},
                "accountHolds.xrp_liquid");
        }
    }

    void
    testAccountHoldsIOU()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const gwFrozen("gwFrozen");
        Account const alice("alice");
        Account const carol("carol");  // no line with gw
        Account const whale("whale");  // large IOU balance
        // sorted pair (hi > lo) to drive getTrustLineBalance's account>issuer (negate) branch
        std::vector<Account> pair{Account("ahsX"), Account("ahsY")};
        std::sort(pair.begin(), pair.end(), [](Account const& a, Account const& b) {
            return a.id() < b.id();
        });
        Account const& lo = pair[0];
        Account const& hi = pair[1];
        Env env(*this);
        env.fund(XRP(1000), gw, gwFrozen, alice, carol, whale, lo, hi);
        env(trust(alice, gw["USD"](1000000)));
        env(pay(gw, alice, gw["USD"](12345)));
        env(trust(gw, alice["USD"](200)));  // gw-side limit -> FullBalance opposite-limit add
        env(trust(alice, gwFrozen["USD"](1000)));
        env(pay(gwFrozen, alice, gwFrozen["USD"](50)));
        env(fset(gwFrozen, asfGlobalFreeze));
        env(trust(whale, gw["USD"](10'000'000'000'000'000)));
        env(pay(gw, whale, gw["USD"](1'000'000'000'000'000)));
        // hi holds lo's USD / lo holds hi's EUR (distinct currencies -> independent lines);
        // each issuer side carries a distinct opposite limit to verify which field is read
        env(trust(hi, lo["USD"](1000)));  // hi (high) limit
        env(trust(lo, hi["USD"](7)));     // lo (low/issuer) limit -> opposite for hi
        env(pay(lo, hi, lo["USD"](77)));
        env(trust(lo, hi["EUR"](1000)));  // lo (low) limit
        env(trust(hi, lo["EUR"](9)));     // hi (high/issuer) limit -> opposite for lo
        env(pay(hi, lo, hi["EUR"](88)));
        env.close();
        auto const& view = *env.current();
        Asset const usd{gw["USD"].issue()};
        Asset const usdFrozen{gwFrozen["USD"].issue()};

        // account == issuer -> limit is effectively infinite (kMaxValue)
        runAccountHolds(
            view,
            gw.id(),
            usd,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::FullBalance,
            STAmount{gw["USD"].issue(), STAmount::kMaxValue, STAmount::kMaxOffset},
            "accountHolds.iou_issuer_full_balance");
        // getLineIfUsable: no trust line -> zero
        runAccountHolds(
            view,
            carol.id(),
            usd,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            gw["USD"](0),
            "accountHolds.iou_no_line");
        // getLineIfUsable: ZeroIfFrozen + global freeze -> zero
        runAccountHolds(
            view,
            alice.id(),
            usdFrozen,
            FreezeHandling::ZeroIfFrozen,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            gwFrozen["USD"](0),
            "accountHolds.iou_frozen_zeroed");
        // getLineIfUsable: ZeroIfFrozen + deep-frozen (not frozen) -> zero (isDeepFrozen)
        {
            Sandbox sb(&*env.current(), TapNone);
            auto line = sb.peek(keylet::line(alice.id(), gw.id(), gw["USD"].issue().currency));
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) | lsfLowDeepFreeze);
            sb.update(line);
            runAccountHolds(
                sb,
                alice.id(),
                usd,
                FreezeHandling::ZeroIfFrozen,
                AuthHandling::IgnoreAuth,
                SpendableHandling::SimpleBalance,
                gw["USD"](0),
                "accountHolds.iou_deep_frozen");
        }
        // getLineIfUsable: IgnoreFreeze keeps the frozen line usable
        runAccountHolds(
            view,
            alice.id(),
            usdFrozen,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            gwFrozen["USD"](50),
            "accountHolds.iou_frozen_ignored");
        // getTrustLineBalance: plain balance
        runAccountHolds(
            view,
            alice.id(),
            usd,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            gw["USD"](12345),
            "accountHolds.iou_balance");
        // getTrustLineBalance: large balance
        runAccountHolds(
            view,
            whale.id(),
            usd,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            gw["USD"](1'000'000'000'000'000),
            "accountHolds.iou_large_balance");
        // getTrustLineBalance: account > issuer -> balance negated into account terms
        runAccountHolds(
            view,
            hi.id(),
            Asset{lo["USD"].issue()},
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            lo["USD"](77),
            "accountHolds.iou_account_high_negate");
        // getTrustLineBalance: account < issuer -> no negate
        runAccountHolds(
            view,
            lo.id(),
            Asset{hi["EUR"].issue()},
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::SimpleBalance,
            hi["EUR"](88),
            "accountHolds.iou_account_low_no_negate");
        // getTrustLineBalance: FullBalance adds the issuer-side (opposite) limit
        runAccountHolds(
            view,
            alice.id(),
            usd,
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::FullBalance,
            gw["USD"](12545),
            "accountHolds.iou_full_balance");
        // oppositeField = sfLowLimit when account > issuer (adds lo's 7, not hi's 1000)
        runAccountHolds(
            view,
            hi.id(),
            Asset{lo["USD"].issue()},
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::FullBalance,
            lo["USD"](84),
            "accountHolds.iou_opposite_low_limit");
        // oppositeField = sfHighLimit when account < issuer (adds hi's 9, not lo's 1000)
        runAccountHolds(
            view,
            lo.id(),
            Asset{hi["EUR"].issue()},
            FreezeHandling::IgnoreFreeze,
            AuthHandling::IgnoreAuth,
            SpendableHandling::FullBalance,
            hi["EUR"](97),
            "accountHolds.iou_opposite_high_limit");
        // getTrustLineBalance: FullBalance add overflows (balance + opposite limit at max)
        {
            Sandbox sb(&*env.current(), TapNone);
            auto line = sb.peek(keylet::line(alice.id(), gw.id(), gw["USD"].issue().currency));
            bool const aliceHigh = alice.id() > gw.id();
            STAmount big{
                line->getFieldAmount(sfBalance).get<Issue>(),
                STAmount::kMaxValue,
                STAmount::kMaxOffset};
            if (aliceHigh)
                big.negate();  // stored low-perspective -> alice's term is +kMaxValue
            line->setFieldAmount(sfBalance, big);
            auto const& oppField = aliceHigh ? sfLowLimit : sfHighLimit;
            line->setFieldAmount(
                oppField,
                STAmount{
                    line->getFieldAmount(oppField).get<Issue>(),
                    STAmount::kMaxValue,
                    STAmount::kMaxOffset});
            sb.update(line);
            runAccountHoldsThrows(
                sb,
                alice.id(),
                usd,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::FullBalance,
                "accountHolds.iou_full_balance_overflow");
        }
    }

    void
    testAccountHoldsMPT()
    {
        using namespace jtx;
        {
            Account const bob("bob");  // holds 100
            Account const dan("dan");  // no token
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob},
                    .create = MPTCreate{
                        .maxAmt = kMaxMpTokenAmount,
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            env.fund(XRP(1000), dan);
            mpt.pay(mpt.issuer(), bob, 100);
            env.close();
            MPTIssue const mptIssue{mpt.issuanceID()};
            Asset const mptAsset{mptIssue};
            MPTIssue const absentIss{makeMptID(99, mpt.issuer().id())};
            auto const& view = *env.current();

            // account == issuer + FullBalance, issuance absent -> zero
            runAccountHolds(
                view,
                mpt.issuer().id(),
                Asset{absentIss},
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::FullBalance,
                STAmount{absentIss},
                "accountHolds.mpt_issuer_no_issuance");
            // account == issuer + FullBalance -> MaximumAmount - OutstandingAmount (near int64 max)
            runAccountHolds(
                view,
                mpt.issuer().id(),
                mptAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::FullBalance,
                STAmount{mptIssue, static_cast<std::uint64_t>(kMaxMpTokenAmount - 100)},
                "accountHolds.mpt_issuer_full_balance");
            // FullBalance on a non-issuer skips the issuer block -> plain balance
            runAccountHolds(
                view,
                bob.id(),
                mptAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::FullBalance,
                STAmount{mptIssue, std::uint64_t{100}},
                "accountHolds.mpt_holder_full_balance");
            // no MPToken -> zero
            runAccountHolds(
                view,
                dan.id(),
                mptAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::SimpleBalance,
                STAmount{mptIssue},
                "accountHolds.mpt_no_token");
            // ZeroIfFrozen + locked issuance -> zero
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mpt.issuanceID()));
                iss->setFieldU32(sfFlags, iss->getFieldU32(sfFlags) | lsfMPTLocked);
                sb.update(iss);
                runAccountHolds(
                    sb,
                    bob.id(),
                    mptAsset,
                    FreezeHandling::ZeroIfFrozen,
                    AuthHandling::IgnoreAuth,
                    SpendableHandling::SimpleBalance,
                    STAmount{mptIssue},
                    "accountHolds.mpt_frozen");
            }
            // plain balance
            runAccountHolds(
                view,
                bob.id(),
                mptAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::SimpleBalance,
                STAmount{mptIssue, std::uint64_t{100}},
                "accountHolds.mpt_balance");
        }
        // holder with the maximum MPT amount (2^63 - 1) reads back unchanged
        {
            Account const rich("mptrich");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw2",
                MPTInit{
                    .holders = {rich},
                    .create = MPTCreate{
                        .maxAmt = kMaxMpTokenAmount,
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            mpt.pay(mpt.issuer(), rich, kMaxMpTokenAmount);
            env.close();
            MPTIssue const richIssue{mpt.issuanceID()};
            runAccountHolds(
                *env.current(),
                rich.id(),
                Asset{richIssue},
                FreezeHandling::IgnoreFreeze,
                AuthHandling::IgnoreAuth,
                SpendableHandling::SimpleBalance,
                STAmount{richIssue, static_cast<std::uint64_t>(kMaxMpTokenAmount)},
                "accountHolds.mpt_max_balance");
        }
        // ZeroIfUnauthorized consults requireAuth on an auth-required issuance
        {
            Account const ada("ada");  // authorized holder
            Account const eve("eve");  // opted in, not authorized
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {ada, eve},
                    .create = MPTCreate{.flags = tfMPTRequireAuth | tfMPTCanTransfer}});
            mpt.authorize({.account = ada});  // ada opts in
            mpt.authorize({.holder = ada});   // issuer authorizes ada
            mpt.authorize({.account = eve});  // eve opts in (stays unauthorized)
            mpt.pay(mpt.issuer(), ada, 100);
            env.close();
            MPTIssue const mptIssue{mpt.issuanceID()};
            Asset const mptAsset{mptIssue};
            auto const& view = *env.current();

            // ZeroIfUnauthorized keeps an authorized holder's balance
            runAccountHolds(
                view,
                ada.id(),
                mptAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::ZeroIfUnauthorized,
                SpendableHandling::SimpleBalance,
                STAmount{mptIssue, std::uint64_t{100}},
                "accountHolds.mpt_authorized");
            // ZeroIfUnauthorized clears an unauthorized holder to zero
            runAccountHolds(
                view,
                eve.id(),
                mptAsset,
                FreezeHandling::IgnoreFreeze,
                AuthHandling::ZeroIfUnauthorized,
                SpendableHandling::SimpleBalance,
                STAmount{mptIssue},
                "accountHolds.mpt_unauthorized");
        }
    }

    void
    runTests() override
    {
        testAccountHoldsXRP();
        testAccountHoldsIOU();
        testAccountHoldsMPT();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAccountHolds, formal_verification, xrpl);

}  // namespace xrpl::test
