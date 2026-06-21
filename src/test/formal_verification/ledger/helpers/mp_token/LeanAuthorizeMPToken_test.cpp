#include <test/formal_verification/ffi/ledger/helpers/MPTokenHelpersFFI.h>
#include <test/formal_verification/ffi/protocol/TerFFI.h>
#include <test/formal_verification/ledger/LedgerSuite.h>
#include <test/jtx.h>
#include <test/jtx/mpt.h>

#include <xrpl/ledger/Sandbox.h>
#include <xrpl/ledger/helpers/MPTokenHelpers.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/TxFlags.h>

#include <cstdint>
#include <limits>
#include <optional>

namespace xrpl::test {

using namespace formal_verification;

class LeanAuthorizeMPToken_test : public LedgerSuite
{
    void
    runAuthorizeMPToken(
        Sandbox& sb,
        XRPAmount const& priorBalance,
        MPTID const& mptID,
        AccountID const& account,
        uint32_t flags,
        std::optional<AccountID> const& holderID,
        TER expected,
        char const* label)
    {
        runLedgerTest(sb, label, [&](LedgerFFI& ledger) {
            TER const cppTer = authorizeMPToken(
                sb,
                priorBalance,
                mptID,
                account,
                beast::Journal{beast::Journal::getNullSink()},
                flags,
                holderID);
            LeanTerResult const leanRes = formal_verification::authorizeMPToken(
                ledger, priorBalance, mptID, account, flags, holderID);

            BEAST_EXPECT(cppTer == expected);
            BEAST_EXPECTS(!leanRes.threw, leanRes.error);
            BEAST_EXPECT(leanRes.value == cppTerByte(cppTer));
        });
    }

    // Setup: issuance with holder (optionally authorized)
    struct setupAuthorizeMPTokenHolder
    {
        jtx::Account const bob{"bob"};
        jtx::Env env;
        jtx::MPTTester mpt;
        Sandbox sb;

        setupAuthorizeMPTokenHolder(
            beast::unit_test::Suite& suite,
            uint32_t flags,
            bool authorize = false)
            : env(suite)
            , mpt(env,
                  "gw",
                  jtx::MPTInit{
                      .holders = {bob},
                      .create =
                          jtx::MPTCreate{
                              .authorize = authorize
                                  ? std::make_optional(std::vector<jtx::Account>{})
                                  : std::nullopt,
                              .flags = flags}})
            , sb(&*env.current(), TapNone)
        {
        }
    };

    static void
    setupAuthorizeMPTokenHolderReserve(setupAuthorizeMPTokenHolder& s, uint32_t ownerCount)
    {
        auto sle = s.sb.peek(keylet::account(s.bob.id()));
        sle->at(sfOwnerCount) = ownerCount;
        s.sb.update(sle);
    }

    static void
    setupAuthorizeMPTokenHolderLocked(setupAuthorizeMPTokenHolder& s, uint64_t locked)
    {
        auto sleMpt = s.sb.peek(keylet::mptoken(s.mpt.issuanceID(), s.bob.id()));
        sleMpt->setFieldU64(sfLockedAmount, locked);
        s.sb.update(sleMpt);
    }

    // account acts on its own token (holderID unset)
    void
    testAuthorizeMPTokenHolder()
    {
        using namespace jtx;
        constexpr int64_t maxDrops = std::numeric_limits<int64_t>::max();
        constexpr int64_t minDrops = std::numeric_limits<int64_t>::min();
        constexpr uint32_t u32max = std::numeric_limits<uint32_t>::max();

        // guard: submitting account absent (no holder involved)
        {
            Account const ghost("ghost");  // never created
            Env env(*this);
            MPTTester mpt(env, "gw", MPTInit{.create = MPTCreate{.flags = tfMPTCanTransfer}});
            Sandbox sb(&*env.current(), TapNone);
            runAuthorizeMPToken(
                sb,
                XRPAmount{0},
                mpt.issuanceID(),
                ghost.id(),
                0,
                std::nullopt,
                tecINTERNAL,
                "authorizeMPToken.account_absent");
        }

        // delete: no MPToken
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                tfMPTUnauthorize,
                std::nullopt,
                tecINTERNAL,
                "authorizeMPToken.holder_unauthorize_no_token");
        }

        // delete: rejected on a nonzero balance
        {
            Account const bob("bob");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{.holders = {bob}, .create = MPTCreate{.flags = tfMPTCanTransfer}});
            mpt.authorize(MPTAuthorize{.account = bob});
            mpt.pay(mpt.issuer(), bob, 100);
            Sandbox sb(&*env.current(), TapNone);
            runAuthorizeMPToken(
                sb,
                XRPAmount{0},
                mpt.issuanceID(),
                bob.id(),
                tfMPTUnauthorize,
                std::nullopt,
                tecINTERNAL,
                "authorizeMPToken.holder_unauthorize_nonzero");
        }

        // delete: rejected on a nonzero locked amount, allowed when zero
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer, true);
            setupAuthorizeMPTokenHolderLocked(s, 5);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                tfMPTUnauthorize,
                std::nullopt,
                tecINTERNAL,
                "authorizeMPToken.holder_unauthorize_locked_nonzero");
        }
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer, true);
            setupAuthorizeMPTokenHolderLocked(s, 0);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                tfMPTUnauthorize,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_unauthorize_locked_zero");
        }

        // delete: empty MPToken erased
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer, true);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                tfMPTUnauthorize,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_unauthorize");
        }

        // create: reserve check (priorBalance < reserveCreate). ownerCount < 2 is free
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            setupAuthorizeMPTokenHolderReserve(s, 1);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_reserve_below_two");
        }
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            setupAuthorizeMPTokenHolderReserve(s, 2);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tecINSUFFICIENT_RESERVE,
                "authorizeMPToken.holder_reserve_at_two");
        }
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            setupAuthorizeMPTokenHolderReserve(s, 2);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{minDrops},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tecINSUFFICIENT_RESERVE,
                "authorizeMPToken.holder_reserve_int64_min");
        }
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            setupAuthorizeMPTokenHolderReserve(s, 2);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{maxDrops},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_reserve_int64_max");
        }

        // create: exact reserve boundary
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            setupAuthorizeMPTokenHolderReserve(s, 2);
            int64_t const reserve = s.sb.fees().accountReserve(3).drops();
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{reserve - 1},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tecINSUFFICIENT_RESERVE,
                "authorizeMPToken.holder_reserve_just_below");
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{reserve},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_reserve_exact");
        }

        // create: ownerCount = uint32 max. `uOwnerCount + 1` wraps to 0 in uint32,
        // tecINSUFFICIENT_RESERVE should be expected?
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            setupAuthorizeMPTokenHolderReserve(s, u32max);
            int64_t const baseReserve = s.sb.fees().accountReserve(0).drops();
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{baseReserve},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_owner_count_max_wrap");
        }

        // create: !mpt || issuer == account guard
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            MPTID const absent = makeMptID(999, s.mpt.issuer().id());
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                absent,
                s.bob.id(),
                0,
                std::nullopt,
                tecINTERNAL,
                "authorizeMPToken.holder_issuance_absent");
        }
        {
            Env env(*this);
            MPTTester mpt(env, "gw", MPTInit{.create = MPTCreate{.flags = tfMPTCanTransfer}});
            Sandbox sb(&*env.current(), TapNone);
            runAuthorizeMPToken(
                sb,
                XRPAmount{0},
                mpt.issuanceID(),
                mpt.issuer().id(),
                0,
                std::nullopt,
                tecINTERNAL,
                "authorizeMPToken.holder_self_issuer");
        }

        // create: success (fresh holder, reserveCreate 0)
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTCanTransfer);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.bob.id(),
                0,
                std::nullopt,
                tesSUCCESS,
                "authorizeMPToken.holder_create");
        }
    }

    // account is the issuer acting on holderID's token: toggles lsfMPTAuthorized.
    void
    testAuthorizeMPTokenIssuer()
    {
        using namespace jtx;

        // guard: issuance absent
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTRequireAuth);
            MPTID const absent = makeMptID(999, s.mpt.issuer().id());
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                absent,
                s.mpt.issuer().id(),
                0,
                s.bob.id(),
                tecINTERNAL,
                "authorizeMPToken.issuer_issuance_absent");
        }

        // guard: account != issuer
        {
            Account const bob("bob");
            Account const carol("carol");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{.holders = {bob, carol}, .create = MPTCreate{.flags = tfMPTRequireAuth}});
            mpt.authorize(MPTAuthorize{.account = bob});
            Sandbox sb(&*env.current(), TapNone);
            runAuthorizeMPToken(
                sb,
                XRPAmount{0},
                mpt.issuanceID(),
                carol.id(),
                0,
                bob.id(),
                tecINTERNAL,
                "authorizeMPToken.issuer_wrong_account");
        }

        // guard: holder has no MPToken
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTRequireAuth);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.mpt.issuer().id(),
                0,
                s.bob.id(),
                tecINTERNAL,
                "authorizeMPToken.issuer_holder_token_absent");
        }

        // success: set lsfMPTAuthorized
        {
            setupAuthorizeMPTokenHolder s(*this, tfMPTRequireAuth, true);
            runAuthorizeMPToken(
                s.sb,
                XRPAmount{0},
                s.mpt.issuanceID(),
                s.mpt.issuer().id(),
                0,
                s.bob.id(),
                tesSUCCESS,
                "authorizeMPToken.issuer_authorize");
        }

        // success: clear lsfMPTAuthorized
        {
            Account const bob("bob");
            Env env(*this);
            MPTTester mpt(
                env,
                "gw",
                MPTInit{.holders = {bob}, .create = MPTCreate{.flags = tfMPTRequireAuth}});
            mpt.authorize(MPTAuthorize{.account = bob});
            mpt.authorize(MPTAuthorize{.holder = bob});
            Sandbox sb(&*env.current(), TapNone);
            runAuthorizeMPToken(
                sb,
                XRPAmount{0},
                mpt.issuanceID(),
                mpt.issuer().id(),
                tfMPTUnauthorize,
                bob.id(),
                tesSUCCESS,
                "authorizeMPToken.issuer_unauthorize");
        }
    }

    void
    runTests() override
    {
        testAuthorizeMPTokenHolder();
        testAuthorizeMPTokenIssuer();
    }
};

BEAST_DEFINE_TESTSUITE(LeanAuthorizeMPToken, formal_verification, xrpl);

}  // namespace xrpl::test
