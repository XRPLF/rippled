#ifndef __LEDGERENGINE__
#define __LEDGERENGINE__

#include <cstring>

#include <boost/shared_ptr.hpp>

#include "uint256.h"
#include "Ledger.h"
#include "Currency.h"
#include "SerializedLedger.h"

// A LedgerEngine handles high-level operations to a ledger

enum LedgerEngineParms
{
	lepCREATE = 1,	// Create if not present
};

class LedgerEngine
{
public:
	typedef boost::shared_ptr<LedgerEngine> pointer;

protected:
	Ledger::pointer mLedger;

public:
	LedgerEngine() { ; }
	LedgerEngine(Ledger::pointer l) : mLedger(l) { ; }

	Ledger::pointer getTargetLedger() { return mLedger; }
	void setTargetLedger(Ledger::pointer ledger) { mLedger = ledger; }

	SerializedLedgerEntry::pointer getAccountRoot(LedgerEngineParms parms, const uint160& accountID);

	SerializedLedgerEntry::pointer getNickname(LedgerEngineParms parms, const std::string& nickname);
	SerializedLedgerEntry::pointer getNickname(LedgerEngineParms parms, const uint256& nickHash);

	SerializedLedgerEntry::pointer getRippleState(LedgerEngineParms parms, const uint160& offeror,
		const uint160& borrower, const Currency& currency);

};

#endif
