#ifndef __LEDGERTIMING__
#define __LEDGERTIMING__

// The number of seconds a ledger may remain idle before closing
#	define LEDGER_IDLE_INTERVAL		15

// The number of seconds a validation remains current after its ledger's close time
// This is a safety to protect against very old validations
#	define LEDGER_MAX_INTERVAL		(LEDGER_IDLE_INTERVAL * 32)

// The number of seconds before a close time that we consider a validation acceptable
// This protects against extreme clock errors
#	define LEDGER_EARLY_INTERVAL	240

// The number of milliseconds we wait minimum to ensure participation
#	define LEDGER_MIN_CONSENSUS		2000

// Initial resolution of ledger close time
#	define LEDGER_TIME_ACCURACY		30

// How often to increase resolution
#	define LEDGER_RES_INCREASE		8

// How often to decrease resolution
#	define LEDGER_RES_DECREASE		1

// How often we check state or change positions (in milliseconds)
#	define LEDGER_GRANULARITY		1000

// Avalanche tuning
#define AV_INIT_CONSENSUS_PCT		50	// percentage of nodes on our UNL that must vote yes

#define AV_MID_CONSENSUS_TIME		50	// percentage of previous close time before we advance
#define AV_MID_CONSENSUS_PCT		65	// percentage of nodes that most vote yes after advancing

#define AV_LATE_CONSENSUS_TIME		85	// percentage of previous close time before we advance
#define AV_LATE_CONSENSUS_PCT		70	// percentage of nodes that most vote yes after advancing


class ContinuousLedgerTiming
{
public:

	static int LedgerTimeResolution[];

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

	static int getNextLedgerTimeResolution(int previousResolution, bool previousAgree, int ledgerSeq);
};

#endif
