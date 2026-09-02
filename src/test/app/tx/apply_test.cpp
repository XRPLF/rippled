// Copyright (c) 2020 Dev Null Productions

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/noop.h>
#include <test/jtx/sig.h>
#include <test/jtx/sponsor.h>

#include <xrpl/basics/Slice.h>
#include <xrpl/basics/StringUtilities.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/tx/apply.h>

#include <functional>
#include <memory>

namespace xrpl {

class Apply_test : public beast::unit_test::Suite
{
public:
    void
    run() override
    {
        testcase("Require Fully Canonical Signature");
        testFullyCanonicalSigs();
        testRoleSignatureCacheIsEraSpecific();
    }

    // A signature verdict reached before fixCleanup3_4_0 must not be honored
    // after it, because the two eras require the sponsor signature to cover
    // different bytes. The two calls below use one HashRouter and differ only in
    // the rules, which is what the flag ledger looks like in practice: relay and
    // submit verify against the validated rules, which lag the open ledger rules
    // that preflight2 verifies against, so one transaction gets checked under
    // both prefixes at the same time.
    void
    testRoleSignatureCacheIsEraSpecific()
    {
        testcase("Role signature cache is era specific");

        using namespace test::jtx;

        Env preFix{*this, testableAmendments() - fixCleanup3_4_0};
        Env postFix{*this, testableAmendments()};
        auto const postFixRules = postFix.current()->rules();

        Account const alice{"alice"};
        Account const sponsor{"sponsor"};
        preFix.fund(XRP(10'000), alice, sponsor);
        preFix.close();

        // Signed while the fix is disabled, so the sponsor signature carries
        // the old prefix, which is shared with the top level signature.
        auto const jt = preFix.jt(
            noop(alice),
            Fee(XRP(1)),
            sponsor::As(sponsor, spfSponsorFee),
            Sig(sfSponsorSignature, sponsor));
        BEAST_EXPECT(jt.stx);
        if (!jt.stx)
            return;

        auto& router = preFix.app().getHashRouter();
        BEAST_EXPECT(
            checkValidity(router, *jt.stx, preFix.current()->rules()).first == Validity::Valid);

        // Same router, asked again under the post-fix rules. The verdict above
        // was reached under the old prefix and must not be reused, or a
        // signature moved between roles would survive the amendment.
        BEAST_EXPECT(checkValidity(router, *jt.stx, postFixRules).first == Validity::SigBad);
    }

    void
    testFullyCanonicalSigs()
    {
        // Construct a payments w/out a fully-canonical tx
        std::string const nonFullyCanonicalTx =
            "12000022000000002400000001201B00497D9C6140000000000F6950684000000"
            "00000000C732103767C7B2C13AD90050A4263745E4BAB2B975417FA22E87780E1"
            "506DDAF21139BE74483046022100E95670988A34C4DB0FA73A8BFD6383872AF43"
            "8C147A62BC8387406298C3EADC1022100A7DC80508ED5A4750705C702A81CBF9D"
            "2C2DC3AFEDBED37BBCCD97BC8C40E08F8114E25A26437D923EEF4D6D815DF9336"
            "8B62E6440848314BB85996936E4F595287774684DC2AC6266024BEF";

        auto ret = strUnHex(nonFullyCanonicalTx);
        SerialIter sitTrans(makeSlice(*ret));  // NOLINT(bugprone-unchecked-optional-access)
        STTx const tx = *std::make_shared<STTx const>(std::ref(sitTrans));

        {
            test::jtx::Env fullyCanonical(*this, test::jtx::testableAmendments());

            Validity const valid =
                checkValidity(
                    fullyCanonical.app().getHashRouter(), tx, fullyCanonical.current()->rules())
                    .first;
            if (valid == Validity::Valid)
                fail("Non-Fully canonical signature was permitted");
        }

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(Apply, tx, xrpl);

}  // namespace xrpl
