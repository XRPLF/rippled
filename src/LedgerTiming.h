#ifndef __LEDGERTIMING__
#define __LEDGERTIMING__

#define LEDGER_CLOSE_FAST
// #define LEDGER_CLOSE_SLOW

#ifdef LEDGER_CLOSE_FAST

// Time between one ledger close and the next ledger close
#	define LEDGER_INTERVAL			30

// Time before we take a position
#	define LEDGER_WOBBLE_TIME		1

// Time we acceleratet avalanche
#	define LEDGER_ACCEL_CONVERGE	10

// Time we permit avalanche to finish
#	define LEDGER_CONVERGE			14

// Maximum converge time
#	define LEDGER_MAX_CONVERGE		20

#define AV_PCT_STOP					85

#endif



// BEGIN LEDGER_CLOSE_CONTINUOUS

// The number of seconds a ledger may remain idle before closing
#	define LEDGER_IDLE_INTERVAL		15

// How long we wait to transition from inactive to active
#	define LEDGER_IDLE_SPIN_TIME	2

// Avalance tuning (percent of UNL voting yes for us to vote yes)
#define AV_MIN_CONSENSUS			55
#define AV_AVG_CONSENSUS			65
#define AV_MAX_CONSENSUS			70


class ContinuousLedgerTiming
{
public:

	// Returns the number of seconds the ledger was or should be open
	// Call when a consensus is reached and when any transaction is relayed to be added
	static int shouldClose(
		bool anyTransactions,
		int previousProposers,		int proposersClosed,
		int previousOpenSeconds,	int currentOpenSeconds);

	static bool haveConsensus(
		int previousProposers,		int currentProposers,
		int currentAgree,			int currentClosed,
		int previousAgreeTime,		int currentAgreeTime);

};

// END LEDGER_CLOSE_CONTINUOUS



#endif
