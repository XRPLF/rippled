#ifndef RIPPLE_LEDGERFORMAT_H
#define RIPPLE_LEDGERFORMAT_H

// Used as the type of a transaction or the type of a ledger entry.
enum LedgerEntryType
{
	ltINVALID			= -1,
	ltACCOUNT_ROOT		= 'a',
	ltDIR_NODE			= 'd',
	ltGENERATOR_MAP		= 'g',
	ltRIPPLE_STATE		= 'r',
	ltNICKNAME			= 'n',
	ltOFFER				= 'o',
	ltCONTRACT			= 'c',
	ltLEDGER_HASHES		= 'h',
	ltFEATURES			= 'f',
	ltFEE_SETTINGS		= 's',
};

// Used as a prefix for computing ledger indexes (keys).
enum LedgerNameSpace
{
	spaceAccount		= 'a',
	spaceDirNode		= 'd',
	spaceGenerator		= 'g',
	spaceNickname		= 'n',
	spaceRipple			= 'r',
	spaceOffer			= 'o',	// Entry for an offer.
	spaceOwnerDir		= 'O',	// Directory of things owned by an account.
	spaceBookDir		= 'B',	// Directory of order books.
	spaceContract		= 'c',
	spaceSkipList		= 's',
	spaceFeature		= 'f',
	spaceFee			= 'e',
};

enum LedgerSpecificFlags
{
	// ltACCOUNT_ROOT
	lsfPasswordSpent	= 0x00010000,	// True, if password set fee is spent.
	lsfRequireDestTag	= 0x00020000,	// True, to require a DestinationTag for payments.
	lsfRequireAuth		= 0x00040000,	// True, to require a authorization to hold IOUs.
	lsfDisallowXRP		= 0x00080000,	// True, to disallow sending XRP.

	// ltOFFER
	lsfPassive			= 0x00010000,

	// ltRIPPLE_STATE
	lsfLowReserve		= 0x00010000,	// True, if entry counts toward reserve.
	lsfHighReserve		= 0x00020000,
	lsfLowAuth			= 0x00040000,
	lsfHighAuth			= 0x00080000,
};

class LedgerEntryFormat
{
public:
	std::string					t_name;
	LedgerEntryType				t_type;
	SOTemplate					elements;

	static std::map<int, LedgerEntryFormat*>			byType;
	static std::map<std::string, LedgerEntryFormat*>	byName;

	LedgerEntryFormat(const char *name, LedgerEntryType type) : t_name(name), t_type(type)
	{
		byName[name] = this;
		byType[type] = this;
	}
	LedgerEntryFormat& operator<<(const SOElement& el)
	{
		elements.push_back(el);
		return *this;
	}

	static LedgerEntryFormat* getLgrFormat(LedgerEntryType t);
	static LedgerEntryFormat* getLgrFormat(const std::string& t);
	static LedgerEntryFormat* getLgrFormat(int t);
};

#endif
// vim:ts=4
