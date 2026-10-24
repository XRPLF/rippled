#include <test/app/invariants/InvariantsBase.h>
#include <test/jtx/Account.h>
#include <test/jtx/Ballot.h>
#include <test/jtx/Env.h>
#include <test/jtx/mpt.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/protocol/ConfidentialTransfer.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Keylet.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STLedgerEntry.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/TxFormats.h>
#include <xrpl/protocol/XRPAmount.h>
#include <xrpl/tx/ApplyContext.h>

#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl::test {

class InvariantsBallot_test : public InvariantsBase
{
    void
    testBallotInvariants()
    {
        using namespace jtx;

        // A token-mode ballot owned by `owner` with one cast by `voter`, in the
        // closed ledger before the precheck runs.
        std::optional<Keylet> ballotKeylet;
        Preclose const precloseBallot = [&](Account const& owner, Account const& voter, Env& env) {
            MPTTester mpt(env, owner, {.holders = {voter}, .fund = false});
            mpt.create({.flags = tfMPTCanTransfer | tfMPTCanLock});
            mpt.authorize({.account = voter});
            mpt.pay(owner, voter, 10);

            auto const tally = ballot::generateKeypair();
            ballotKeylet = keylet::ballot(owner.id(), env.seq(owner));
            env(createTokenBallot(
                owner,
                mpt.issuanceID(),
                tally,
                2,
                0,
                env.now().time_since_epoch().count() + 1000,
                ballotKeylet->key));
            env.close();
            env(castVote(
                voter,
                ballotKeylet->key,
                makeBallotCast(
                    tally,
                    2,
                    0,
                    10,
                    getBallotCastContextHash(voter.id(), ballotKeylet->key, env.seq(voter)))));
            auto const sle = env.le(*ballotKeylet);
            return BEAST_EXPECT(sle && (*sle)[sfVoteCount] == 1);
        };

        auto const peekBallot = [&](ApplyContext& ac) -> SLE::pointer {
            if (!ballotKeylet)
                return nullptr;
            return ac.view().peek(*ballotKeylet);
        };

        STTx const tx{ttACCOUNT_SET, [](STObject&) {}};
        std::initializer_list<TER> const failTers{tecINVARIANT_FAILED, tefINVARIANT_FAILED};

        testcase("Ballot tally length");
        doInvariantCheck(
            {{"ballot tally length != OptionCount"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                STArray shorter;
                shorter.push_back(sle->getFieldArray(sfEncryptedTally)[0]);
                sle->setFieldArray(sfEncryptedTally, shorter);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);

        testcase("Ballot tally removed");
        doInvariantCheck(
            {{"ballot tally length != OptionCount"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                sle->makeFieldAbsent(sfEncryptedTally);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);

        testcase("Ballot results without finalize");
        doInvariantCheck(
            {{"ballot results present without finalize flag"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                STArray results;
                for (int i = 0; i < 2; ++i)
                {
                    STObject o = STObject::makeInnerObject(sfBallotResult);
                    o.setFieldU64(sfBallotWeight, 0);
                    results.push_back(std::move(o));
                }
                sle->setFieldArray(sfResults, results);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);

        testcase("Ballot results length");
        doInvariantCheck(
            {{"ballot results length != OptionCount"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                STArray results;
                STObject o = STObject::makeInnerObject(sfBallotResult);
                o.setFieldU64(sfBallotWeight, 10);
                results.push_back(std::move(o));
                sle->setFieldArray(sfResults, results);
                sle->setFlag(lsfBallotFinalized);
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);

        testcase("Ballot VoteCount decreased");
        doInvariantCheck(
            {{"ballot VoteCount decreased"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                (*sle)[sfVoteCount] = 0u;
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);

        testcase("Ballot VoteCount without a cast");
        doInvariantCheck(
            {{"ballot VoteCount delta != BallotVotes created"}},
            [&](Account const&, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                (*sle)[sfVoteCount] = (*sle)[sfVoteCount] + 1u;
                ac.view().update(sle);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);

        testcase("BallotVote without its ballot");
        doInvariantCheck(
            {{"BallotVote created without ballot update"}},
            [&](Account const& owner, Account const&, ApplyContext& ac) {
                auto const sle = peekBallot(ac);
                if (!sle)
                    return false;
                auto const vote = std::make_shared<SLE>(keylet::ballotVote(sle->key(), owner.id()));
                (*vote)[sfAccount] = owner.id();
                (*vote)[sfBallotID] = sle->key();
                (*vote)[sfBallotWeight] = 1;
                vote->setFieldArray(sfEncryptedVotes, sle->getFieldArray(sfEncryptedTally));
                (*vote)[sfOwnerNode] = 0;
                ac.view().insert(vote);
                return true;
            },
            XRPAmount{},
            tx,
            failTers,
            precloseBallot);
    }

public:
    void
    run() override
    {
        testBallotInvariants();
    }
};

BEAST_DEFINE_TESTSUITE(InvariantsBallot, app, xrpl);

}  // namespace xrpl::test
