#include "Ledger.h"
#include "RippleState.h"

/*
This pulls all the ripple lines of a given account out of the ledger.
It provides a vector so you to easily iterate through them
*/

class RippleLines
{
	std::vector<RippleState::pointer> mLines;
	void fillLines(const uint160& accountID, Ledger::ref ledger);
public:

	RippleLines(const uint160& accountID, Ledger::ref ledger);
	RippleLines(const uint160& accountID ); // looks in the current ledger

	std::vector<RippleState::pointer>& getLines() { return(mLines); }
	void printRippleLines();
};
