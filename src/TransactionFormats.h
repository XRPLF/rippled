#ifndef __TRANSACTIONFORMATS__
#define __TRANSACTIONFORMATS__

#include "SerializedObject.h"

enum TransactionType
{
	ttINVALID			= -1,
	ttPAYMENT			= 0,
	ttCLAIM				= 1,
	ttWALLET_ADD		= 2,
	ttACCOUNT_SET		= 3,
	ttPASSWORD_FUND		= 4,
	ttPASSWORD_SET		= 5,
	ttNICKNAME_SET		= 6,
	ttCREDIT_SET		= 20,
	ttTRANSIT_SET		= 21,
	ttINVOICE			= 10,
	ttOFFER				= 11,
};

struct TransactionFormat
{
	const char *t_name;
	TransactionType t_type;
	SOElement elements[16];
};

const int TransactionISigningPubKey	= 0;
const int TransactionISourceID		= 1;
const int TransactionISequence		= 2;
const int TransactionIType			= 3;
const int TransactionIFee			= 4;

const int TransactionMinLen			= 32;
const int TransactionMaxLen			= 1048576;

// Transaction flags.
const uint32 tfCreateAccount		= 0x00010000;
const uint32 tfNoRippleDirect		= 0x00020000;

const uint32 tfUnsetEmailHash		= 0x00010000;
const uint32 tfUnsetWalletLocator	= 0x00020000;

extern TransactionFormat InnerTxnFormats[];
extern TransactionFormat* getTxnFormat(TransactionType t);
#endif
// vim:ts=4
