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

class LeanAccountSendMulti_test : public LedgerSuite
{
    void
    runAccountSendMulti(
        Sandbox& sb,
        AccountID const& sender,
        Asset const& asset,
        MultiplePaymentDestinations const& receivers,
        TER expected,
        char const* label,
        WaiveTransferFee waive = WaiveTransferFee::No)
    {
        auto const aSenderPre = holdingSTAmount(sb, sender, asset);
        auto const mSenderPre = holdingMPTAmount(sb, sender, asset);
        std::vector<std::optional<STAmount>> aRecvPre;
        std::vector<std::optional<std::uint64_t>> mRecvPre;
        for (auto const& r : receivers)
        {
            aRecvPre.push_back(holdingSTAmount(sb, r.first, asset));
            mRecvPre.push_back(holdingMPTAmount(sb, r.first, asset));
        }
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = accountSendMulti(
                sb, sender, asset, receivers, beast::Journal{beast::Journal::getNullSink()}, waive);
            LeanTerResult const leanRes =
                formal_verification::accountSendMulti(ledger, sender, asset, receivers, waive);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
        expectHoldingDir(aSenderPre, holdingSTAmount(sb, sender, asset), true, label);
        expectHoldingDir(mSenderPre, holdingMPTAmount(sb, sender, asset), true, label);
        for (std::size_t i = 0; i < receivers.size(); ++i)
        {
            auto const& rid = receivers[i].first;
            if (rid == sender)
                continue;
            expectHoldingDir(aRecvPre[i], holdingSTAmount(sb, rid, asset), false, label);
            expectHoldingDir(mRecvPre[i], holdingMPTAmount(sb, rid, asset), false, label);
        }
        expectNoNegativeNativeBalance(sb, label);
    }

    // accountSendMulti(sender, ...), then assert the `sender` ownerCount delta (-1 iff a
    // per-receiver directSendNoFeeIOU reserve-clear fired and deleted the sender's line).
    void
    runAccountSendMultiOwnerDelta(
        Sandbox& sb,
        jtx::Account const& sender,
        Asset const& asset,
        MultiplePaymentDestinations const& receivers,
        int ownerDelta,
        char const* label)
    {
        auto const acct = keylet::account(sender.id());
        int const before = static_cast<int>(sb.read(acct)->getFieldU32(sfOwnerCount));
        runAccountSendMulti(sb, sender.id(), asset, receivers, tesSUCCESS, label);
        BEAST_EXPECT(
            static_cast<int>(sb.read(acct)->getFieldU32(sfOwnerCount)) - before == ownerDelta);
    }

    void
    testAccountSendMultiXRP()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Env env(*this);
        env.fund(XRP(10'000), alice, bob, carol);
        env.close();
        Asset const xrp{xrpIssue()};

        // normal multi-send: alice -> {bob: 1 XRP, carol: 2 XRP}
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{1'000'000}}, {carol.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp");
        }
        // receiver == sender is skipped, the other is credited
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{alice.id(), Number{1'000'000}}, {bob.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_self_skip");
        }
        // zero amount is skipped
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{0}}, {carol.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_zero_skip");
        }
        // null sender (kZero): credit receivers, no debit
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                xrpAccount(),
                xrp,
                {{bob.id(), Number{1'000'000}}, {carol.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_null_sender");
        }
        // sum exceeds the sender balance: receivers credited, then tecFAILED_PROCESSING
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{8'000'000'000}}, {carol.id(), Number{8'000'000'000}}},
                tecFAILED_PROCESSING,
                "accountSendMulti.xrp_insufficient");
        }
        // negative amount: per-receiver guard -> tecINTERNAL
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{-5}}, {carol.id(), Number{1'000'000}}},
                tecINTERNAL,
                "accountSendMulti.xrp_negative");
        }
        // null (kZero) receiver in the list is skipped, the other is credited
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{xrpAccount(), Number{1'000'000}}, {bob.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_null_receiver");
        }
        // extreme: a receiver credited to exactly the native cap kMaxNativeN
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(bob.id()));
            sle->setFieldAmount(
                sfBalance, STAmount{XRPAmount{std::int64_t(STAmount::kMaxNativeN) - 1'000'000}});
            sb.update(sle);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{1'000'000}}, {carol.id(), Number{1'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_max_balance");
        }
        // extreme: a receiver credited beyond the cap (both sides store the raw int64)
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(bob.id()));
            sle->setFieldAmount(
                sfBalance, STAmount{XRPAmount{std::int64_t(STAmount::kMaxNativeN)}});
            sb.update(sle);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{1'000'000}}, {carol.id(), Number{1'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_overflow");
        }
        // sender non-kZero but absent from the ledger: peek misses, no debit, receivers credited
        {
            Account const ghost("ghostms");
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                ghost.id(),
                xrp,
                {{bob.id(), Number{1'000'000}}, {carol.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_sender_missing");
        }
        // receiver non-kZero but absent from the ledger: peek misses, that one is skipped
        {
            Account const ghost("ghostmr");
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{ghost.id(), Number{1'000'000}}, {bob.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_receiver_missing");
        }
        // kZero sender and a kZero receiver: the kZero receiver skips via senderID ==
        // receiverID, the kZero sender does no debit, the real receiver is credited
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                xrpAccount(),
                xrp,
                {{xrpAccount(), Number{1'000'000}}, {bob.id(), Number{2'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_both_null");
        }
    }

    // Known-failing balance/holding-invariant flag (multi): a receiver credited past
    // INT64_MAX wraps to INT64_MIN on both sides, leaving a negative, decreased balance.
    void
    testAccountSendMultiXRPHoldingInvariant()
    {
        using namespace jtx;
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Env env(*this);
        env.fund(XRP(10'000), alice, bob, carol);
        env.close();
        Asset const xrp{xrpIssue()};

        // single receiver credited past INT64_MAX: the credit add wraps on both sides
        {
            Sandbox sb(&*env.current(), TapNone);
            auto sle = sb.peek(keylet::account(bob.id()));
            sle->setFieldAmount(
                sfBalance,
                STAmount{std::uint64_t(std::numeric_limits<std::int64_t>::max()), false});
            sb.update(sle);
            runAccountSendMulti(
                sb,
                alice.id(),
                xrp,
                {{bob.id(), Number{1}}, {carol.id(), Number{1'000'000}}},
                tesSUCCESS,
                "accountSendMulti.xrp_receiver_int64");
        }
    }

    void
    testAccountSendMultiIOU()
    {
        using namespace jtx;
        Account const gw("gw");
        Account const alice("alice");
        Account const bob("bob");
        Account const carol("carol");
        Env env(*this);
        env.fund(XRP(10'000), gw, alice, bob, carol);
        env.close();
        env.trust(gw["USD"](1'000'000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, gw["USD"](1000)));
        env(rate(gw, 1.25));
        env.close();
        Asset const usd{gw["USD"].issue()};

        // issuer -> {alice, bob}: direct issue to both
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                gw.id(),
                usd,
                {{alice.id(), Number{100}}, {bob.id(), Number{200}}},
                tesSUCCESS,
                "accountSendMulti.iou_issue");
        }
        // holder -> {bob, carol}: transit, fee waived
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                usd,
                {{bob.id(), Number{50}}, {carol.id(), Number{30}}},
                tesSUCCESS,
                "accountSendMulti.iou_transit_waived",
                WaiveTransferFee::Yes);
        }
        // holder -> {bob, carol}: transit, rate 1.25 accumulates into takeFromSender
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                usd,
                {{bob.id(), Number{50}}, {carol.id(), Number{30}}},
                tesSUCCESS,
                "accountSendMulti.iou_transit_fee");
        }
        // mixed: alice -> {gw (direct redeem), bob (transit)}
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                usd,
                {{gw.id(), Number{40}}, {bob.id(), Number{30}}},
                tesSUCCESS,
                "accountSendMulti.iou_mixed",
                WaiveTransferFee::Yes);
        }
        // zero-amount receiver is skipped, the other transits
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                usd,
                {{bob.id(), Number{0}}, {carol.id(), Number{30}}},
                tesSUCCESS,
                "accountSendMulti.iou_skip",
                WaiveTransferFee::Yes);
        }
        // transit to an account with no line: trustCreate finds no account -> tefINTERNAL
        {
            Account const ghost("ghostm");
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                usd,
                {{ghost.id(), Number{30}}, {bob.id(), Number{20}}},
                tefINTERNAL,
                "accountSendMulti.iou_no_account",
                WaiveTransferFee::Yes);
        }
        // direct issue to an account with no line: trustCreate finds no account -> tefINTERNAL
        {
            Account const ghost("ghostd");
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                gw.id(),
                usd,
                {{ghost.id(), Number{30}}, {bob.id(), Number{20}}},
                tefINTERNAL,
                "accountSendMulti.iou_direct_no_account");
        }
        // extreme: issue near kMaxValue, the balance add renormalizes (rounding)
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                gw.id(),
                usd,
                {{alice.id(), Number{9'999'999'999'999'999}}, {bob.id(), Number{100}}},
                tesSUCCESS,
                "accountSendMulti.iou_large");
        }
        // issue to a holder with no line: the per-receiver directSendNoFeeIOU trustCreates
        {
            Account const dave("dave");
            env.fund(XRP(10'000), dave);
            env.close();
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                gw.id(),
                usd,
                {{dave.id(), Number{100}}, {bob.id(), Number{50}}},
                tesSUCCESS,
                "accountSendMulti.iou_create_line");
        }
        // total transited to receivers far exceeds kMaxValue: the IOU accumulation renorms
        {
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                alice.id(),
                usd,
                {{bob.id(), Number{9'999'999'999'999'999}},
                 {carol.id(), Number{9'999'999'999'999'999}}},
                tesSUCCESS,
                "accountSendMulti.iou_total_large",
                WaiveTransferFee::Yes);
        }
    }

    void
    testAccountSendMultiMPT()
    {
        using namespace jtx;
        {
            Account const bob("bob");
            Account const carol("carol");
            Account const dan("dan");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol, dan},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            mpt.pay(mpt.issuer(), bob, 1000);
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const issuer = mpt.issuer().id();
            auto const mptID = mptAsset.get<MPTIssue>().getMptID();

            // issuance absent: tecOBJECT_NOT_FOUND
            {
                Sandbox sb(&*env.current(), TapNone);
                MPTIssue const absent{makeMptID(99, issuer)};
                runAccountSendMulti(
                    sb,
                    issuer,
                    Asset{absent},
                    {{bob.id(), Number{100}}, {carol.id(), Number{100}}},
                    tecOBJECT_NOT_FOUND,
                    "accountSendMulti.mpt_absent");
            }
            // negative amount: per-receiver guard -> tecINTERNAL
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{bob.id(), Number{-5}}, {carol.id(), Number{100}}},
                    tecINTERNAL,
                    "accountSendMulti.mpt_negative");
            }
            // zero-amount receiver is skipped, the other is issued
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{bob.id(), Number{0}}, {carol.id(), Number{100}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_skip");
            }
            // receiver == sender is skipped (senderID == receiverID), the other transits
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{bob.id(), Number{10}}, {carol.id(), Number{30}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_self_skip",
                    WaiveTransferFee::Yes);
            }
            // direct: issuer -> {bob, carol}, aggregate OutstandingAmount += sum
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{bob.id(), Number{100}}, {carol.id(), Number{200}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_issue");
            }
            // direct issue to a receiver with no MPToken: the issuer leg returns tecNO_AUTH
            {
                Account const fred("fred");
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{fred.id(), Number{10}}, {carol.id(), Number{20}}},
                    tecNO_AUTH,
                    "accountSendMulti.mpt_direct_no_auth");
            }
            // extreme: a receiver balance at UINT64_MAX, the credit add overflows -> tecINTERNAL
            {
                Sandbox sb(&*env.current(), TapNone);
                auto tok = sb.peek(keylet::mptoken(mptID, carol.id()));
                tok->setFieldU64(sfMPTAmount, std::numeric_limits<std::uint64_t>::max());
                sb.update(tok);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{carol.id(), Number{1}}, {bob.id(), Number{100}}},
                    tecINTERNAL,
                    "accountSendMulti.mpt_receiver_overflow");
            }
            // receiver == issuer: a direct redeem leg (OutstandingAmount -= amt)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{issuer, Number{100}}, {carol.id(), Number{50}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_redeem",
                    WaiveTransferFee::Yes);
            }
            // transit: holder -> {carol, dan}, fee waived
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{carol.id(), Number{50}}, {dan.id(), Number{30}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_transit_waived",
                    WaiveTransferFee::Yes);
            }
            // transit to a receiver with no MPToken: the issuer leg returns tecNO_AUTH
            {
                Account const ed("ed");
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{ed.id(), Number{10}}, {carol.id(), Number{20}}},
                    tecNO_AUTH,
                    "accountSendMulti.mpt_transit_no_auth",
                    WaiveTransferFee::Yes);
            }
            // transit issuer leg overflows OutstandingAmount (staged UINT64_MAX) -> tecPATH_DRY
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mptID));
                iss->setFieldU64(sfOutstandingAmount, std::numeric_limits<std::uint64_t>::max());
                sb.update(iss);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{carol.id(), Number{1}}, {dan.id(), Number{1}}},
                    tecPATH_DRY,
                    "accountSendMulti.mpt_transit_outstanding_overflow",
                    WaiveTransferFee::Yes);
            }
            // transit: receivers credited, then the bulk sender debit is insufficient
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{carol.id(), Number{600}}, {dan.id(), Number{600}}},
                    tecINSUFFICIENT_FUNDS,
                    "accountSendMulti.mpt_insufficient",
                    WaiveTransferFee::Yes);
            }
            // transit sender with no MPToken: the bulk sender->issuer debit -> tecNO_AUTH
            {
                Account const gus("gus");
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    gus.id(),
                    mptAsset,
                    {{carol.id(), Number{10}}, {dan.id(), Number{10}}},
                    tecNO_AUTH,
                    "accountSendMulti.mpt_sender_no_auth",
                    WaiveTransferFee::Yes);
            }
        }
        // transit with a transfer fee: rate 1.2 charged into the bulk sender debit
        {
            Account const bob("bob");
            Account const carol("carol");
            Account const dan("dan");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol, dan},
                    .create = MPTCreate{
                        .transferFee = 20000,
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            mpt.pay(mpt.issuer(), bob, 1000);
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            // fee charged: rate 1.2 means the sender pays 180 to deliver 150
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{carol.id(), Number{100}}, {dan.id(), Number{50}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_transit_fee");
            }
            // same fee'd token, waived: sender pays only 150 (discriminates the waive flag)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    bob.id(),
                    mptAsset,
                    {{carol.id(), Number{100}}, {dan.id(), Number{50}}},
                    tesSUCCESS,
                    "accountSendMulti.mpt_transit_fee_waived",
                    WaiveTransferFee::Yes);
            }
        }
        // issuer-send aggregate MaximumAmount check (3 conditions, ordered to guard the
        // unsigned subtractions).
        {
            Account const bob("bob");
            Account const carol("carol");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol},
                    .create = MPTCreate{
                        .maxAmt = 1000,
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const issuer = mpt.issuer().id();
            auto const mptID = mptAsset.get<MPTIssue>().getMptID();

            // condition 1: a single send alone exceeds the cap (2000 > 1000)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{bob.id(), Number{2000}}, {carol.id(), Number{50}}},
                    tecPATH_DRY,
                    "accountSendMulti.mpt_overflow_sendamount");
            }
            // condition 2: the aggregate of sends exceeds the cap (600 + 600 > 1000)
            {
                Sandbox sb(&*env.current(), TapNone);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{bob.id(), Number{600}}, {carol.id(), Number{600}}},
                    tecPATH_DRY,
                    "accountSendMulti.mpt_aggregate_overflow");
            }
            // condition 3: outstanding + aggregate exceeds the cap (outstanding staged 999,
            // 1 issued, the 2nd send tips it over)
            {
                Sandbox sb(&*env.current(), TapNone);
                auto iss = sb.peek(keylet::mptIssuance(mptID));
                iss->setFieldU64(sfOutstandingAmount, 999);
                sb.update(iss);
                runAccountSendMulti(
                    sb,
                    issuer,
                    mptAsset,
                    {{bob.id(), Number{1}}, {carol.id(), Number{1}}},
                    tecPATH_DRY,
                    "accountSendMulti.mpt_outstanding_overflow");
            }
        }
        // aggregate cap defaults to kMaxMpTokenAmount (INT64_MAX): each send is below it but
        // their sum is not (condition 2 at the default cap) -> tecPATH_DRY
        {
            Account const bob("bob");
            Account const carol("carol");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{
                    .holders = {bob, carol},
                    .create = MPTCreate{
                        .authorize = std::make_optional(std::vector<Account>{}),
                        .flags = tfMPTCanTransfer}});
            Asset const mptAsset{MPTIssue{mpt.issuanceID()}};
            auto const issuer = mpt.issuer().id();
            Sandbox sb(&*env.current(), TapNone);
            runAccountSendMulti(
                sb,
                issuer,
                mptAsset,
                {{bob.id(), Number{4'611'686'018'427'387'904}},
                 {carol.id(), Number{4'611'686'018'427'387'904}}},
                tecPATH_DRY,
                "accountSendMulti.mpt_total_int64");
        }
    }

    void
    testAccountSendMultiIOUReserveClear()
    {
        using namespace jtx;
        std::vector<Account> pair{Account("mrc1"), Account("mrc2")};
        std::sort(pair.begin(), pair.end(), [](Account const& a, Account const& b) {
            return a.id() < b.id();
        });
        Account const lo = pair[0];
        Account const hi = pair[1];
        Account const dummy("mrcd");  // skipped second receiver (amount 0)
        Env env(*this);
        env.fund(XRP(10'000), lo, hi, dummy);
        env.close();
        auto const cur = lo["USD"].issue().currency;
        auto const lineK = keylet::line(lo.id(), hi.id(), cur);
        auto const j = beast::Journal{beast::Journal::getNullSink()};
        auto deliver = [&](Sandbox& sb, Account const& iss, Account const& holder) {
            BEAST_EXPECT(accountSend(sb, iss.id(), holder.id(), iss["USD"](100), j) == tesSUCCESS);
        };
        // holder redeems amt to iss as one leg of a multi
        auto redeem = [&](Sandbox& sb,
                          Account const& holder,
                          Account const& iss,
                          std::int64_t amt,
                          int delta,
                          char const* label) {
            runAccountSendMultiOwnerDelta(
                sb,
                holder,
                Asset{iss["USD"].issue()},
                {{iss.id(), Number{amt}}, {dummy.id(), Number{0}}},
                delta,
                label);
        };

        // all conjuncts true: the if fires, ownerCount drops by 1 (line deleted)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            redeem(sb, lo, hi, 100, -1, "accountSendMulti.reserve_all_true");
        }
        // A false: saBefore <= 0 (balance staged to 0 before the full redeem)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldAmount(sfBalance, STAmount{Issue{cur, noAccount()}, 0});
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_saBefore");
        }
        // B false: saBalance > 0 after (partial redeem keeps a positive balance)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            redeem(sb, lo, hi, 50, 0, "accountSendMulti.reserve_no_saBalance");
        }
        // C false: sender reserve flag clear
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) & ~lsfLowReserve);
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_reserveFlag");
        }
        // D false: line no-ripple == account default-ripple (set lsfLowNoRipple)
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) | lsfLowNoRipple);
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_noRippleDiff");
        }
        // E false: line frozen
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfFlags, line->getFieldU32(sfFlags) | lsfLowFreeze);
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_notFrozen");
        }
        // F false (low): sender trust limit nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldAmount(sfLowLimit, STAmount{Issue{cur, lo.id()}, 1000});
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_zeroLimit_lo");
        }
        // G false (low): sender quality in nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfLowQualityIn, 1u);
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_qualityIn_lo");
        }
        // H false (low): sender quality out nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, hi, lo);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfLowQualityOut, 1u);
            sb.update(line);
            redeem(sb, lo, hi, 100, 0, "accountSendMulti.reserve_no_qualityOut_lo");
        }

        // F false (high): sender trust limit nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, lo, hi);
            auto line = sb.peek(lineK);
            line->setFieldAmount(sfHighLimit, STAmount{Issue{cur, hi.id()}, 1000});
            sb.update(line);
            redeem(sb, hi, lo, 100, 0, "accountSendMulti.reserve_no_zeroLimit_hi");
        }
        // G false (high): sender quality in nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, lo, hi);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfHighQualityIn, 1u);
            sb.update(line);
            redeem(sb, hi, lo, 100, 0, "accountSendMulti.reserve_no_qualityIn_hi");
        }
        // H false (high): sender quality out nonzero
        {
            Sandbox sb(&*env.current(), TapNone);
            deliver(sb, lo, hi);
            auto line = sb.peek(lineK);
            line->setFieldU32(sfHighQualityOut, 1u);
            sb.update(line);
            redeem(sb, hi, lo, 100, 0, "accountSendMulti.reserve_no_qualityOut_hi");
        }
    }

    void
    runTests() override
    {
        testAccountSendMultiXRP();
        testAccountSendMultiXRPHoldingInvariant();
        testAccountSendMultiIOU();
        testAccountSendMultiMPT();
        testAccountSendMultiIOUReserveClear();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAccountSendMulti, formal_verification, xrpl);

}  // namespace xrpl::test
