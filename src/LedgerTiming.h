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


#endif

#ifdef LEDGER_CLOSE_SLOW

#	define LEDGER_INTERVAL			1800

#	define LEDGER_FORCE_CONVERGE	180

#	define LEDGER_CONVERGE			240


// Time a transaction must be unconflicted before we consider it protected
#	define LEDGER_PROTECT		90

#endif

// Avalance tuning (percent of UNL voting yes for us to vote yes)
#define AV_MIN_CONSENSUS			50
#define AV_AVG_CONSENSUS			60
#define AV_MAX_CONSENSUS			70

// We consider consensus reached at this percent agreement
#define AV_PCT_STOP					90


#endif
