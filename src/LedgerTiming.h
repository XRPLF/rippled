#ifndef __LEDGERTIMING__
#define __LEDGERTIMING__

#define LEDGER_CLOSE_FAST
// #define LEDGER_CLOSE_SLOW

#ifdef LEDGER_CLOSE_FAST

// Time between one ledger close and the next ledger close
#	define LEDGER_INTERVAL			60

// Time we expect avalanche to finish
#	define LEDGER_CONVERGE			20

// Time we forcibly abort avalanche
#	define LEDGER_FORCE_CONVERGE	30

#endif

#ifdef LEDGER_CLOSE_SLOW

#	define LEDGER_INTERVAL			1800

#	define LEDGER_CONVERGE			180

#	define LEDGER_FORCE_CONVERGE	240

// Time a transaction must be unconflicted before we consider it protected
#	define LEDGER_PROTECT		90

#endif

#define MIN_CONSENSUS			50
#define AVG_CONSENSUS			60
#define MAX_CONSENSUS			70

#endif
