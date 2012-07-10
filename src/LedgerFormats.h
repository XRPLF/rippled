#ifndef __LEDGERFORMATS__
#define __LEDGERFORMATS__

#include "SerializedObject.h"

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
};

// Used as a prefix for computing ledger indexes (keys).
enum LedgerNameSpace
{
	spaceAccount		= 'a',
	spaceDirNode		= 'd',
	spaceGenerator		= 'g',
	spaceNickname		= 'n',
	spaceRipple			= 'r',
	spaceRippleDir		= 'R',
	spaceOffer			= 'o',	// Entry for an offer.
	spaceOwnerDir		= 'O',	// Directory of things owned by an account.
	spaceBookDir		= 'B',	// Directory of order books.
	spaceBond			= 'b',
	spaceInvoice		= 'i',
};

enum LedgerSpecificFlags
{
	// ltACCOUNT_ROOT
	lsfPasswordSpent	= 0x00010000,	// True if password set fee is spent.

	// ltOFFER
	lsfPassive			= 0x00010000,

	// ltRIPPLE_STATE
	lsfLowIndexed		= 0x00010000,
	lsfHighIndexed		= 0x00020000,
};

struct LedgerEntryFormat
{
	const char *t_name;
	LedgerEntryType t_type;
	SOElement elements[20];
};

extern LedgerEntryFormat LedgerFormats[];
extern LedgerEntryFormat* getLgrFormat(LedgerEntryType t);
#endif
// vim:ts=4
