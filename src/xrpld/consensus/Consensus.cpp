#include <xrpld/consensus/Consensus.h>

#include <xrpl/basics/Log.h>

namespace ripple {

bool
shouldCloseLedger(
    bool anyTransactions,
    std::size_t prevProposers,
    std::size_t proposersClosed,
    std::size_t proposersValidated,
    std::chrono::milliseconds prevRoundTime,
    std::chrono::milliseconds
        timeSincePrevClose,              // Time since last ledger's close time
    std::chrono::milliseconds openTime,  // Time waiting to close this ledger
    std::chrono::milliseconds idleInterval,
    ConsensusParms const& parms,
    beast::Journal j,
    std::unique_ptr<std::stringstream> const& clog)
{
    CLOG(clog) << "shouldCloseLedger params anyTransactions: "
               << anyTransactions << ", prevProposers: " << prevProposers
               << ", proposersClosed: " << proposersClosed
               << ", proposersValidated: " << proposersValidated
               << ", prevRoundTime: " << prevRoundTime.count() << "ms"
               << ", timeSincePrevClose: " << timeSincePrevClose.count() << "ms"
               << ", openTime: " << openTime.count() << "ms"
               << ", idleInterval: " << idleInterval.count() << "ms"
               << ", ledgerMIN_CLOSE: " << parms.ledgerMIN_CLOSE.count() << "ms"
               << ". ";
    using namespace std::chrono_literals;
    if ((prevRoundTime < -1s) || (prevRoundTime > 10min) ||
        (timeSincePrevClose > 10min))
    {
        // These are unexpected cases, we just close the ledger
        std::stringstream ss;
        ss << "shouldCloseLedger Trans=" << (anyTransactions ? "yes" : "no")
           << " Prop: " << prevProposers << "/" << proposersClosed
           << " Secs: " << timeSincePrevClose.count()
           << " (last: " << prevRoundTime.count() << ")";

        JLOG(j.warn()) << ss.str();
        CLOG(clog) << "closing ledger: " << ss.str() << ". ";
        return true;
    }

    if ((proposersClosed + proposersValidated) > (prevProposers / 2))
    {
        // If more than half of the network has closed, we close
        JLOG(j.trace()) << "Others have closed";
        CLOG(clog) << "closing ledger because enough others have already. ";
        return true;
    }

    if (!anyTransactions)
    {
        // Only close at the end of the idle interval
        CLOG(clog) << "no transactions, returning. ";
        return timeSincePrevClose >= idleInterval;  // normal idle
    }

    // Preserve minimum ledger open time
    if (openTime < parms.ledgerMIN_CLOSE)
    {
        JLOG(j.debug()) << "Must wait minimum time before closing";
        CLOG(clog) << "not closing because under ledgerMIN_CLOSE. ";
        return false;
    }

    // Don't let this ledger close more than twice as fast as the previous
    // ledger reached consensus so that slower validators can slow down
    // the network
    if (openTime < (prevRoundTime / 2))
    {
        JLOG(j.debug()) << "Ledger has not been open long enough";
        CLOG(clog) << "not closing because not open long enough. ";
        return false;
    }

    // Close the ledger
    CLOG(clog) << "no reason to not close. ";
    return true;
}

bool
checkConsensusReached(
    std::size_t agreeing,
    std::size_t total,
    bool count_self,
    std::size_t minConsensusPct,
    bool reachedMax,
    bool stalled,
    std::unique_ptr<std::stringstream> const& clog)
{
    CLOG(clog) << "checkConsensusReached params: agreeing: " << agreeing
               << ", total: " << total << ", count_self: " << count_self
               << ", minConsensusPct: " << minConsensusPct
               << ", reachedMax: " << reachedMax << ". ";

    // If we are alone for too long, we have consensus.
    // Delaying consensus like this avoids a circumstance where a peer
    // gets ahead of proposers insofar as it has not received any proposals.
    // This could happen if there's a slowdown in receiving proposals. Reaching
    // consensus prematurely in this way means that the peer will likely desync.
    // The check for reachedMax should allow plenty of time for proposals to
    // arrive, and there should be no downside. If a peer is truly not
    // receiving any proposals, then there should be no hurry. There's
    // really nowhere to go.
    if (total == 0)
    {
        if (reachedMax)
        {
            CLOG(clog)
                << "Consensus reached because nobody shares our position and "
                   "maximum duration has passed.";
            return true;
        }
        CLOG(clog) << "Consensus not reached and nobody shares our position. ";
        return false;
    }

    // We only get stalled when there are disputed transactions and all of them
    // unequivocally have 80% (minConsensusPct) agreement, either for or
    // against. That is: either under 20% or over 80% consensus (repectively
    // "nay" or "yay"). This prevents manipulation by a minority of byzantine
    // peers of which transactions make the cut to get into the ledger.
    if (stalled)
    {
        CLOG(clog) << "consensus stalled. ";
        return true;
    }

    if (count_self)
    {
        ++agreeing;
        ++total;
        CLOG(clog) << "agreeing and total adjusted: " << agreeing << ','
                   << total << ". ";
    }

    std::size_t currentPercentage = (agreeing * 100) / total;

    CLOG(clog) << "currentPercentage: " << currentPercentage;
    bool const ret = currentPercentage >= minConsensusPct;
    if (ret)
    {
        CLOG(clog) << ", consensus reached. ";
    }
    else
    {
        CLOG(clog) << ", consensus not reached. ";
    }
    return ret;
}

ConsensusState
checkConsensus(
    std::size_t prevProposers,
    std::size_t currentProposers,
    std::size_t currentAgree,
    std::size_t currentFinished,
    std::chrono::milliseconds previousAgreeTime,
    std::chrono::milliseconds currentAgreeTime,
    bool stalled,
    ConsensusParms const& parms,
    bool proposing,
    beast::Journal j,
    std::unique_ptr<std::stringstream> const& clog)
{
    CLOG(clog) << "checkConsensus: prop=" << currentProposers << "/"
               << prevProposers << " agree=" << currentAgree
               << " validated=" << currentFinished
               << " time=" << currentAgreeTime.count() << "/"
               << previousAgreeTime.count() << " proposing? " << proposing
               << " minimum duration to reach consensus: "
               << parms.ledgerMIN_CONSENSUS.count() << "ms"
               << " max consensus time " << parms.ledgerMAX_CONSENSUS.count()
               << "ms"
               << " minimum consensus percentage: " << parms.minCONSENSUS_PCT
               << ". ";

    if (currentAgreeTime <= parms.ledgerMIN_CONSENSUS)
    {
        CLOG(clog) << "Not reached. ";
        return ConsensusState::No;
    }

    if (currentProposers < (prevProposers * 3 / 4))
    {
        // Less than 3/4 of the last ledger's proposers are present; don't
        // rush: we may need more time.
        if (currentAgreeTime < (previousAgreeTime + parms.ledgerMIN_CONSENSUS))
        {
            JLOG(j.trace()) << "too fast, not enough proposers";
            CLOG(clog) << "Too fast, not enough proposers. Not reached. ";
            return ConsensusState::No;
        }
    }

    // Have we, together with the nodes on our UNL list, reached the threshold
    // to declare consensus?
    if (checkConsensusReached(
            currentAgree,
            currentProposers,
            proposing,
            parms.minCONSENSUS_PCT,
            currentAgreeTime > parms.ledgerMAX_CONSENSUS,
            stalled,
            clog))
    {
        JLOG((stalled ? j.warn() : j.debug()))
            << "normal consensus" << (stalled ? ", but stalled" : "");
        CLOG(clog) << "reached" << (stalled ? ", but stalled." : ".");
        return ConsensusState::Yes;
    }

    // Have sufficient nodes on our UNL list moved on and reached the threshold
    // to declare consensus?
    if (checkConsensusReached(
            currentFinished,
            currentProposers,
            false,
            parms.minCONSENSUS_PCT,
            currentAgreeTime > parms.ledgerMAX_CONSENSUS,
            false,
            clog))
    {
        JLOG(j.warn()) << "We see no consensus, but 80% of nodes have moved on";
        CLOG(clog) << "We see no consensus, but 80% of nodes have moved on";
        return ConsensusState::MovedOn;
    }

    std::chrono::milliseconds const maxAgreeTime =
        previousAgreeTime * parms.ledgerABANDON_CONSENSUS_FACTOR;
    if (currentAgreeTime > std::clamp(
                               maxAgreeTime,
                               parms.ledgerMAX_CONSENSUS,
                               parms.ledgerABANDON_CONSENSUS))
    {
        JLOG(j.warn()) << "consensus taken too long";
        CLOG(clog) << "Consensus taken too long. ";
        // Note the Expired result may be overridden by the caller.
        return ConsensusState::Expired;
    }

    // no consensus yet
    JLOG(j.trace()) << "no consensus";
    CLOG(clog) << "No consensus. ";
    return ConsensusState::No;
}

}  // namespace ripple
