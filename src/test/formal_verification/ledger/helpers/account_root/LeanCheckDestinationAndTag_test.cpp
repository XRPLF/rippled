#include <test/formal_verification/ffi/ledger/helpers/AccountRootHelpersFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/AccountRootHelpers.h>
#include <xrpl/protocol/Indexes.h>

#include <memory>
#include <optional>

namespace xrpl::test {

using namespace formal_verification;

class LeanCheckDestinationAndTag_test : public LedgerSuite
{
    void
    runCheckDestinationAndTag(
        Sandbox& sb,
        std::shared_ptr<SLE const> const& toSle,
        bool hasDestinationTag,
        TER expected,
        char const* label)
    {
        std::optional<AccountRootFFI> dstFFI;
        if (toSle)
            dstFFI = AccountRootFFIBuilder()
                         .fromCpp(ledger_entries::AccountRoot(toSle))
                         .build(toSle->key());
        runLedgerTest(sb, label, [&](LedgerFFI const& ledger) {
            TER const cppTer = checkDestinationAndTag(toSle, hasDestinationTag);
            LeanTerResult const leanRes = formal_verification::checkDestinationAndTag(
                ledger, dstFFI ? &*dstFFI : nullptr, hasDestinationTag);
            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    void
    testCheckDestinationAndTag()
    {
        using namespace jtx;
        Env env(*this);
        Account const alice("alice");  // no RequireDest
        Account const bob("bob");      // requires destination tag
        env.fund(XRP(1000), alice, bob);
        env(fset(bob, asfRequireDest));
        env.close();

        Sandbox sb(&*env.current(), TapNone);
        auto const aliceSle = sb.read(keylet::account(alice.id()));
        auto const bobSle = sb.read(keylet::account(bob.id()));
        runCheckDestinationAndTag(sb, nullptr, false, tecNO_DST, "checkDestinationAndTag.no_dst");
        runCheckDestinationAndTag(
            sb, bobSle, false, tecDST_TAG_NEEDED, "checkDestinationAndTag.tag_needed");
        runCheckDestinationAndTag(
            sb, bobSle, true, tesSUCCESS, "checkDestinationAndTag.tag_provided");
        runCheckDestinationAndTag(
            sb, aliceSle, false, tesSUCCESS, "checkDestinationAndTag.no_require");
    }

    void
    runTests() override
    {
        testCheckDestinationAndTag();
    }
};

BEAST_DEFINE_TESTSUITE(LeanCheckDestinationAndTag, formal_verification, xrpl);

}  // namespace xrpl::test
