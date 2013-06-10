#ifndef RIPPLE_ACCOUNTSTATE_H
#define RIPPLE_ACCOUNTSTATE_H

//
// Provide abstract access to an account's state, such that access to the serialized format is hidden.
//

class AccountState
{
public:
	typedef boost::shared_ptr<AccountState> pointer;

public:
	explicit AccountState(const RippleAddress& naAccountID); // For new accounts
	AccountState (SLE::ref ledgerEntry,const RippleAddress& naAccountI);	// For accounts in a ledger

	bool	bHaveAuthorizedKey()
	{
		return mLedgerEntry->isFieldPresent(sfRegularKey);
	}

	RippleAddress getAuthorizedKey()
	{
		return mLedgerEntry->getFieldAccount(sfRegularKey);
	}

	STAmount getBalance() const { return mLedgerEntry->getFieldAmount(sfBalance); }
	uint32 getSeq() const { return mLedgerEntry->getFieldU32(sfSequence); }

	SerializedLedgerEntry::pointer getSLE() { return mLedgerEntry; }
	const SerializedLedgerEntry& peekSLE() const { return *mLedgerEntry; }
	SerializedLedgerEntry& peekSLE() { return *mLedgerEntry; }

	Blob getRaw() const;
	void addJson(Json::Value& value);
	void dump();

	static std::string createGravatarUrl(uint128 uEmailHash);

private:
	RippleAddress					mAccountID;
	RippleAddress					mAuthorizedKey;
	SerializedLedgerEntry::pointer	mLedgerEntry;

	bool							mValid;
};

#endif
// vim:ts=4
