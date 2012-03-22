#ifndef __TRANSACTIONFORMATS__
#define __TRANSACTIONFORMATS__

#include "SerializedObject.h"

#define STI_ACCOUNT STI_HASH160

enum TransactionType
{
	ttINVALID=-1,
	ttMAKE_PAYMENT=0,
	ttINVOICE=1,
	ttEXCHANGE_OFFER=2
};

struct TransactionFormat
{
	const char *t_name;
	TransactionType t_type;
	SOElement elements[16];
};

const int32 TransactionMagic=0x54584E00;
const int TransactionMinLen=32;
const int TransactionMaxLen=1048576;

extern TransactionFormat InnerTxnFormats[];
extern TransactionFormat* getFormat(TransactionType t);
#endif
