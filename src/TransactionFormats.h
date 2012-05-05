#ifndef __TRANSACTIONFORMATS__
#define __TRANSACTIONFORMATS__

#include "SerializedObject.h"

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

const int TransactionIVersion=0, TransactionISigningPubKey=1, TransactionISequence=2;
const int TransactionIType=3, TransactionIFee=4;

const int TransactionMinLen=32;
const int TransactionMaxLen=1048576;

extern TransactionFormat InnerTxnFormats[];
extern TransactionFormat* getTxnFormat(TransactionType t);
#endif
// vim:ts=4
