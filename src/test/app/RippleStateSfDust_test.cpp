#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/TestHelpers.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/basics/Number.h>
#include <xrpl/basics/strHex.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Zero.h>
#include <xrpl/protocol/AccountID.h>
#include <xrpl/protocol/Asset.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STNumber.h>  // IWYU pragma: keep
#include <xrpl/protocol/UintTypes.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>

// Tests for the sfDust field defined in this PR: read-side default value
// on an untouched trust line, and the SoeDefault byte-encoding contract
// that keeps pre-amendment and post-amendment ledgers hash-compatible for
// any RippleState that has never carried non-zero dust.
//
// Kept intentionally free of DustSplit and vault_dust:: references — the
// writer path lives in follow-up PRs.

namespace xrpl::test {

class RippleStateSfDust_test : public beast::unit_test::Suite
{
    FeatureBitset const all_{jtx::testableAmendments()};

    void
    testAbsentReadsZero()
    {
        testcase("sfDust absent on an untouched trust line reads as zero");

        using namespace jtx;
        Env env{*this, all_};
        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(1'000), alice, bob);
        env.close();
        PrettyAsset const asset = alice["USD"];
        env(trust(bob, asset(1'000)));
        env.close();

        auto const line =
            env.le(keylet::trustLine(alice.id(), bob.id(), asset.raw().get<Issue>().currency));
        if (!BEAST_EXPECT(line))
            return;
        BEAST_EXPECT(!line->isFieldPresent(sfDust));
        BEAST_EXPECT(Number{line->at(sfDust)} == beast::kZero);
    }

    // Golden-byte compatibility contract for sfDust:
    //
    //   • sfDust is declared SoeDefault(0) in ledger_entries.macro.
    //   • SoeDefault semantics: absent field == field set to default
    //     value, both encode to the identical byte string. This is the
    //     property that lets a pre-amendment ledger and a post-amendment
    //     ledger co-exist for RippleState entries that have never carried
    //     non-zero dust.
    //
    // If a future refactor accidentally strips SoeDefault, or a codec
    // change reserialises absent-and-zero differently, this test fires
    // immediately with a byte-diff, preserving ledger hashability of
    // existing state.
    void
    testGoldenByteCompat()
    {
        testcase("sfDust SoeDefault byte-encoding contract");

        using namespace jtx;
        Env const env{*this};  // amendment status is irrelevant for this test

        Account const alice{"alice_g"};
        Account const issuer{"issuer_g"};

        AccountID const aliceId = alice.id();
        AccountID const issuerId = issuer.id();
        auto const [low, high] = std::minmax(aliceId, issuerId);
        Issue const usd{toCurrency("USD"), high};
        Keylet const kl = keylet::trustLine(low, high, usd.currency);

        auto const makeSle = [&](std::optional<Number> dustValue) {
            auto sle = std::make_shared<SLE>(kl);
            sle->setFieldAmount(sfBalance, STAmount{usd, 0});
            sle->setFieldAmount(sfLowLimit, STAmount{Issue{usd.currency, low}, 1000});
            sle->setFieldAmount(sfHighLimit, STAmount{Issue{usd.currency, high}, 1000});
            sle->setFieldU32(sfPreviousTxnLgrSeq, 42u);
            sle->setFieldH256(sfPreviousTxnID, uint256{7u});
            if (dustValue)
                sle->at(sfDust) = *dustValue;
            return sle;
        };

        auto const bytesOf = [](std::shared_ptr<SLE> const& sle) {
            return strHex(sle->getSerializer().peekData());
        };

        // Case A: sfDust absent (SoeDefault path — never set on the SLE).
        auto const bytesAbsent = bytesOf(makeSle(std::nullopt));
        // Case B: sfDust explicitly assigned zero (should be collapsed
        // to absent by SoeDefault).
        auto const bytesZero = bytesOf(makeSle(Number{0}));
        // Case C: sfDust explicitly non-zero (post-dust-op shape).
        auto const bytesNonzero = bytesOf(makeSle(Number{7, -12}));

        BEAST_EXPECTS(
            bytesAbsent == bytesZero,
            "SoeDefault contract broken: absent-sfDust encoding (" + bytesAbsent +
                ") differs from explicit-zero encoding (" + bytesZero + ")");
        BEAST_EXPECT(bytesAbsent != bytesNonzero);
        BEAST_EXPECT(bytesZero != bytesNonzero);
    }

public:
    void
    run() override
    {
        testAbsentReadsZero();
        testGoldenByteCompat();
    }
};

BEAST_DEFINE_TESTSUITE(RippleStateSfDust, app, xrpl);

}  // namespace xrpl::test
