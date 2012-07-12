
#include "LedgerTiming.h"

#include <cassert>

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
	if (!anyTransactions)
	{ // no transactions so far this interval
		if (proposersClosed > (previousProposers / 4)) // did we miss a transaction?
			return currentSeconds;
		if (previousSeconds > (LEDGER_IDLE_INTERVAL + 2)) // the last ledger was very slow to close
			return previousSeconds - 1;
		return LEDGER_IDLE_INTERVAL; // normal idle
	}

	if (previousSeconds == LEDGER_IDLE_INTERVAL) // coming out of idle, close now
		return currentSeconds;

	// If the network is slow, try to synchronize close times
	if (previousSeconds > 8)
		return (currentSeconds - currentSeconds % 4);
	else if (previousSeconds > 4)
		return (currentSeconds - currentSeconds % 2);

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
	if (currentAgreeTime <= LEDGER_MIN_CONSENSUS)
		return false;

	if (currentProposers < (previousProposers * 3 / 4))
	{ // Less than 3/4 of the last ledger's proposers are present, we may need more time
		if (currentAgreeTime < (previousAgreeTime + 2))
			return false;
	}

	// If 80% of current proposers (plus us) agree on a set, we have consensus
	if (((currentAgree * 100 + 100) / (currentProposers + 1)) > 80)
		return true;

	// If 50% of the nodes on your UNL (minus us) have closed, you should close
	if (((currentClosed * 100 - 100) / (currentProposers + 1)) > 50)
		return true;

	// no consensus yet
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
