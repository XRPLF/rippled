
#include "LedgerTiming.h"

#include <cassert>

#include <boost/format.hpp>

#include "Log.h"

// NOTE: First and last times must be repeated
int ContinuousLedgerTiming::LedgerTimeResolution[] = { 10, 10, 20, 30, 60, 90, 120, 120 };

// Called when a ledger is open and no close is in progress -- when a transaction is received and no close
// is in process, or when a close completes. Returns the number of seconds the ledger should be be open.
int ContinuousLedgerTiming::shouldClose(
	bool anyTransactions,
	int previousProposers,		// proposers in the last closing
	int proposersClosed,		// proposers who have currently closed this ledgers
	int previousSeconds,		// seconds the previous ledger took to reach consensus
	int currentSeconds)			// seconds since the previous ledger closed
{
	Log(lsTRACE) << boost::str(boost::format("CLC::shouldClose Trans=%s, Prop: %d/%d, Secs: %d/%d") %
		(anyTransactions ? "yes" : "no") % previousProposers % proposersClosed % previousSeconds % currentSeconds);

	if (!anyTransactions)
	{ // no transactions so far this interval
		if (proposersClosed > (previousProposers / 4)) // did we miss a transaction?
		{
			Log(lsTRACE) << "no transactions, many proposers: now";
			return currentSeconds;
		}
		if (previousSeconds > (LEDGER_IDLE_INTERVAL + 2)) // the last ledger was very slow to close
		{
			Log(lsTRACE) << "slow to close";
			return previousSeconds - 1;
		}
		Log(lsTRACE) << "normal idle";
		return LEDGER_IDLE_INTERVAL; // normal idle
	}

	if (previousSeconds == LEDGER_IDLE_INTERVAL) // coming out of idle, close now
	{
		Log(lsTRACE) << "leaving idle, close now";
		return currentSeconds;
	}

	Log(lsTRACE) << "close now";
	return currentSeconds; // this ledger should close now
}

// Returns whether we have a consensus or not. If so, we expect all honest nodes
// to already have everything they need to accept a consensus. Our vote is 'locked in'.
bool ContinuousLedgerTiming::haveConsensus(
	int previousProposers,		// proposers in the last closing (not including us)
	int currentProposers,		// proposers in this closing so far (not including us)
	int currentAgree,			// proposers who agree with us
	int currentClosed,			// proposers who have currently closed their ledgers
	int previousAgreeTime,		// how long it took to agree on the last ledger
	int currentAgreeTime)		// how long we've been trying to agree
{
	Log(lsTRACE) << boost::str(boost::format("CLC::haveConsensus: prop=%d/%d agree=%d closed=%d time=%d/%d") %
		previousProposers % currentProposers % currentAgree % currentClosed % previousAgreeTime % currentAgreeTime);

	if (currentAgreeTime <= LEDGER_MIN_CONSENSUS)
	{
		Log(lsTRACE) << "too fast";
		return false;
	}

	if (currentProposers < (previousProposers * 3 / 4))
	{ // Less than 3/4 of the last ledger's proposers are present, we may need more time
		if (currentAgreeTime < (previousAgreeTime + 2))
		{
			Log(lsTRACE) << "too fast, not enough proposers";
			return false;
		}
	}

	// If 80% of current proposers (plus us) agree on a set, we have consensus
	if (((currentAgree * 100 + 100) / (currentProposers + 1)) > 80)
	{
		Log(lsTRACE) << "normal consensus";
		return true;
	}

	// If 50% of the nodes on your UNL (minus us) have closed, you should close
	if (((currentClosed * 100 - 100) / (currentProposers + 1)) > 50)
	{
		Log(lsTRACE) << "many closers";
		return true;
	}

	// no consensus yet
	Log(lsTRACE) << "no consensus";
	return false;
}

int ContinuousLedgerTiming::getNextLedgerTimeResolution(int previousResolution, bool previousAgree, int ledgerSeq)
{
	assert(ledgerSeq);
	if ((!previousAgree) && ((ledgerSeq % LEDGER_RES_DECREASE) == 0))
	{ // reduce resolution
		int i = 1;
		while (LedgerTimeResolution[i] != previousResolution)
			++i;
		return LedgerTimeResolution[i - 1];
	}

	if ((previousAgree) && ((ledgerSeq % LEDGER_RES_INCREASE) == 0))
	{ // increase resolution
		int i = 1;
		while (LedgerTimeResolution[i] != previousResolution)
			++i;
		return LedgerTimeResolution[i + 1];
	}

	return previousResolution;
}
