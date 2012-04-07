#ifndef __LEDGERFORMATS__
#define __LEDGERFORMATS__

#include "SerializedObject.h"

enum LedgerEntryType
{
	ltINVALID=-1,
	ltACCOUNT_ROOT=0,
	ltRIPPLE_STATE=1,
	ltNICKNAME=2
};

struct LedgerEntryFormat
{
	const char *t_name;
	LedgerEntryType t_type;
	SOElement elements[16];
};

extern LedgerEntryFormat LedgerFormats[];
extern LedgerEntryFormat* getLgrFormat(LedgerEntryType t);
#endif
