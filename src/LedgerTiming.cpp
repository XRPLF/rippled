
#include "LedgerTiming.h"


// Returns the number of seconds the ledger was or should be open
int ContinuousLedgerTiming::shouldClose(	// How many:
	bool anyTransactions,
	int previousProposers,		// proposers there were in the last closing
	int proposersClosed,		// proposers who have currently closed their ledgers
	int previousOpenSeconds,	// seconds the previous ledger was open
	int currentOpenSeconds)		// seconds since the previous ledger closed
{
	if (!anyTransactions)
	{ // no transactions so far this interval
		if (previousOpenSeconds > (LEDGER_IDLE_INTERVAL + 2)) // the last ledger was very slow to close
			return previousOpenSeconds - 1;
		return LEDGER_IDLE_INTERVAL; // normal idle
	}

	if (previousOpenSeconds == LEDGER_IDLE_INTERVAL) // coming out of idle, close now
		return currentOpenSeconds;

	// If the network is slow, try to synchronize close times
	if (previousOpenSeconds > 8)
		return (currentOpenSeconds - currentOpenSeconds % 4);
	else if (previousOpenSeconds > 4)
		return (currentOpenSeconds - currentOpenSeconds % 2);

	return currentOpenSeconds; // this ledger should close now
}

bool ContinuousLedgerTiming::haveConsensus(
	int previousProposers,		// proposers in the last closing (not including us)
	int currentProposers,		// proposers in this closing so far (not including us)
	int currentAgree,			// proposers who agree with us
	int currentClosed,			// proposers who have currently closed their ledgers
	int previousAgreeTime,		// how long it took to agree on the last ledger
	int currentAgreeTime)		// how long we've been trying to agree
{
	if (currentProposers < (previousProposers * 3 / 4))
	{ // Less than 3/4 of the validators are present, slow down
		if (currentAgreeTime < (previousAgreeTime + 2))
			return false;
	}

	// If 80% of current proposers (plus us) agree on a set, we have consensus
	int agreeWeight = (currentAgree * 100 + 100) / (currentProposers + 1);
	if (agreeWeight > 80)
		return true;

	// If 50% of the nodes on your UNL (minus us) have closed, you should close
	int closeWeight = (currentClosed * 100 - 100) / (currentProposers + 1);
	if (closeWeight > 50)
		return true;

	return false;
}
	