/** @file
 *  Policy functions that drive the XRP Ledger consensus phase transitions.
 *
 *  Contains three pure decision functions — `shouldCloseLedger`,
 *  `checkConsensusReached`, and `checkConsensus` — that consume observable
 *  network state and return boolean or enum results. All mutable consensus
 *  state lives in the `Consensus<Adaptor>` template in `Consensus.h`; this
 *  file holds only the timing and agreement policies that are independently
 *  testable as free functions.
 */
#include <xrpld/consensus/Consensus.h>

#include <xrpld/consensus/ConsensusParms.h>
#include <xrpld/consensus/ConsensusTypes.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <sstream>

namespace xrpl {

bool
shouldCloseLedger(
    bool anyTransactions,
    std::size_t prevProposers,
    std::size_t proposersClosed,
    std::size_t proposersValidated,
    std::chrono::milliseconds prevRoundTime,
    std::chrono::milliseconds timeSincePrevClose,
    std::chrono::milliseconds openTime,
    std::chrono::milliseconds idleInterval,
    ConsensusParms const& parms,
    beast::Journal j,
    std::unique_ptr<std::stringstream> const& clog)
{
    CLOG(clog) << "shouldCloseLedger params anyTransactions: " << anyTransactions
               << ", prevProposers: " << prevProposers << ", proposersClosed: " << proposersClosed
               << ", proposersValidated: " << proposersValidated
               << ", prevRoundTime: " << prevRoundTime.count() << "ms"
               << ", timeSincePrevClose: " << timeSincePrevClose.count() << "ms"
               << ", openTime: " << openTime.count() << "ms"
               << ", idleInterval: " << idleInterval.count() << "ms"
               << ", ledgerMIN_CLOSE: " << parms.ledgerMIN_CLOSE.count() << "ms"
               << ". ";
    using namespace std::chrono_literals;
    if ((prevRoundTime < -1s) || (prevRoundTime > 10min) || (timeSincePrevClose > 10min))
    {
        std::stringstream ss;
        ss << "shouldCloseLedger Trans=" << (anyTransactions ? "yes" : "no")
           << " Prop: " << prevProposers << "/" << proposersClosed
           << " Secs: " << timeSincePrevClose.count() << " (last: " << prevRoundTime.count() << ")";

        JLOG(j.warn()) << ss.str();
        CLOG(clog) << "closing ledger: " << ss.str() << ". ";
        return true;
    }

    if ((proposersClosed + proposersValidated) > (prevProposers / 2))
    {
        JLOG(j.trace()) << "Others have closed";
        CLOG(clog) << "closing ledger because enough others have already. ";
        return true;
    }

    if (!anyTransactions)
    {
        CLOG(clog) << "no transactions, returning. ";
        return timeSincePrevClose >= idleInterval;
    }

    if (openTime < parms.ledgerMIN_CLOSE)
    {
        JLOG(j.debug()) << "Must wait minimum time before closing";
        CLOG(clog) << "not closing because under ledgerMIN_CLOSE. ";
        return false;
    }

    // Don't close more than twice as fast as the previous round: slower
    // validators must be able to keep up, and this rate-limit prevents a
    // fast node from driving the network beyond their capacity.
    if (openTime < (prevRoundTime / 2))
    {
        JLOG(j.debug()) << "Ledger has not been open long enough";
        CLOG(clog) << "not closing because not open long enough. ";
        return false;
    }

    CLOG(clog) << "no reason to not close. ";
    return true;
}

/** Determine whether a raw vote count satisfies the consensus threshold.
 *
 *  Internal helper shared by `checkConsensus()` for two distinct queries:
 *  "have we reached agreement?" and "have enough peers moved on?". The two
 *  calls differ only in which counters are passed and whether `countSelf` is
 *  set.
 *
 *  @param agreeing       Number of peers whose position matches ours.
 *  @param total          Total number of current proposers (excluding self).
 *  @param countSelf      If `true`, add one to both `agreeing` and `total`
 *      before computing the percentage. Pass `proposing` from
 *      `checkConsensus()`; observers never count themselves.
 *  @param minConsensusPct Percentage threshold required to declare consensus
 *      (typically `ConsensusParms::minCONSENSUS_PCT` = 80).
 *  @param reachedMax     `true` when `currentAgreeTime > ledgerMAX_CONSENSUS`.
 *      Used only in the zero-peer path to guard against premature self-close.
 *  @param stalled        `true` when all disputed transactions have unambiguous
 *      supermajority agreement either for or against inclusion. Bypasses the
 *      percentage check and returns `true` immediately to prevent a Byzantine
 *      minority from manipulating which transactions make the cut.
 *  @param clog           Optional log buffer; appended when non-null.
 *  @return `true` if consensus is considered reached, `false` otherwise.
 *  @note When `total == 0` (no peer proposals received yet), the function
 *      returns `false` until `reachedMax` is set. This guards against
 *      prematurely closing on a solo position before any network proposals
 *      have arrived, which would likely cause a desync once peers are heard.
 */
bool
checkConsensusReached(
    std::size_t agreeing,
    std::size_t total,
    bool countSelf,
    std::size_t minConsensusPct,
    bool reachedMax,
    bool stalled,
    std::unique_ptr<std::stringstream> const& clog)
{
    CLOG(clog) << "checkConsensusReached params: agreeing: " << agreeing << ", total: " << total
               << ", count_self: " << countSelf << ", minConsensusPct: " << minConsensusPct
               << ", reachedMax: " << reachedMax << ". ";

    if (total == 0)
    {
        if (reachedMax)
        {
            CLOG(clog) << "Consensus reached because nobody shares our position and "
                          "maximum duration has passed.";
            return true;
        }
        CLOG(clog) << "Consensus not reached and nobody shares our position. ";
        return false;
    }

    if (stalled)
    {
        CLOG(clog) << "consensus stalled. ";
        return true;
    }

    if (countSelf)
    {
        ++agreeing;
        ++total;
        CLOG(clog) << "agreeing and total adjusted: " << agreeing << ',' << total << ". ";
    }

    std::size_t const currentPercentage = (agreeing * 100) / total;

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
    CLOG(clog) << "checkConsensus: prop=" << currentProposers << "/" << prevProposers
               << " agree=" << currentAgree << " validated=" << currentFinished
               << " time=" << currentAgreeTime.count() << "/" << previousAgreeTime.count()
               << " proposing? " << proposing
               << " minimum duration to reach consensus: " << parms.ledgerMIN_CONSENSUS.count()
               << "ms"
               << " max consensus time " << parms.ledgerMAX_CONSENSUS.count() << "ms"
               << " minimum consensus percentage: " << parms.minCONSENSUS_PCT << ". ";

    if (currentAgreeTime <= parms.ledgerMIN_CONSENSUS)
    {
        CLOG(clog) << "Not reached. ";
        return ConsensusState::No;
    }

    if (currentProposers < (prevProposers * 3 / 4))
    {
        // Fewer than 75 % of the previous round's proposers are visible;
        // wait an extra ledgerMIN_CONSENSUS before acting to allow laggards
        // to arrive. Rushing here would exclude slow-but-honest validators.
        if (currentAgreeTime < (previousAgreeTime + parms.ledgerMIN_CONSENSUS))
        {
            JLOG(j.trace()) << "too fast, not enough proposers";
            CLOG(clog) << "Too fast, not enough proposers. Not reached. ";
            return ConsensusState::No;
        }
    }

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
    if (currentAgreeTime >
        std::clamp(maxAgreeTime, parms.ledgerMAX_CONSENSUS, parms.ledgerABANDON_CONSENSUS))
    {
        JLOG(j.warn()) << "consensus taken too long";
        CLOG(clog) << "Consensus taken too long. ";
        // Note the Expired result may be overridden by the caller.
        return ConsensusState::Expired;
    }

    JLOG(j.trace()) << "no consensus";
    CLOG(clog) << "No consensus. ";
    return ConsensusState::No;
}

}  // namespace xrpl
