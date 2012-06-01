#ifndef __TRANSACTIONFORMATS__
#define __TRANSACTIONFORMATS__

#include "SerializedObject.h"

enum TransactionType
{
	ttINVALID			= -1,
	ttPAYMENT			= 0,
	ttCLAIM				= 1,
	ttINVOICE			= 2,
	ttOFFER				= 3,
	ttCREDIT_SET		= 20,
	ttTRANSIT_SET		= 21,
};

struct TransactionFormat
{
	const char *t_name;
	TransactionType t_type;
	SOElement elements[16];
};

const int32 TransactionMagic		= 0x54584E00;	// 'TXN'

const int TransactionISigningPubKey	= 0;
const int TransactionISourceID		= 1;
const int TransactionISequence		= 2;
const int TransactionIType			= 3;
const int TransactionIFee			= 4;

const int TransactionMinLen			= 32;
const int TransactionMaxLen			= 1048576;

// Transaction flags.
const uint32 tfCreateAccount		= 0x00010000;

extern TransactionFormat InnerTxnFormats[];
extern TransactionFormat* getTxnFormat(TransactionType t);
#endif
// vim:ts=4
