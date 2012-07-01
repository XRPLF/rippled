#ifndef __LEDGERTIMING__
#define __LEDGERTIMING__

// The number of seconds a ledger may remain idle before closing
#	define LEDGER_IDLE_INTERVAL		15

// The number of seconds a validation remains current
#	define LEDGER_MAX_INTERVAL		60

// The number of seconds we wait minimum to ensure participation
#	define LEDGER_MIN_CONSENSUS		2

// Avalanche tuning
#define AV_INIT_CONSENSUS_PCT		50	// percentage of nodes on our UNL that must vote yes

#define AV_MID_CONSENSUS_TIME		50	// percentage of previous close time before we advance
#define AV_MID_CONSENSUS_PCT		65	// percentage of nodes that most vote yes after advancing

#define AV_LATE_CONSENSUS_TIME		85	// percentage of previous close time before we advance
#define AV_LATE_CONSENSUS_PCT		70	// percentage of nodes that most vote yes after advancing

class ContinuousLedgerTiming
{
public:

	// Returns the number of seconds the ledger was or should be open
	// Call when a consensus is reached and when any transaction is relayed to be added
	static int shouldClose(
		bool anyTransactions,
		int previousProposers,		int proposersClosed,
		int previousSeconds,		int currentSeconds);

	static bool haveConsensus(
		int previousProposers,		int currentProposers,
		int currentAgree,			int currentClosed,
		int previousAgreeTime,		int currentAgreeTime);

};

#endif
