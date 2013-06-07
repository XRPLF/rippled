#ifndef NICKNAMESTATE_H
#define NICKNAMESTATE_H

//
// State of a nickname node.
// - Isolate ledger entry format.
//

class NicknameState
{
public:
	typedef boost::shared_ptr<NicknameState> pointer;

private:
	SerializedLedgerEntry::pointer	mLedgerEntry;

public:
	NicknameState(SerializedLedgerEntry::pointer ledgerEntry);	// For accounts in a ledger

	bool					haveMinimumOffer() const;
	STAmount				getMinimumOffer() const;
	RippleAddress			getAccountID() const;

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	Blob getRaw() const;
	void addJson(Json::Value& value);
};

#endif
// vim:ts=4
