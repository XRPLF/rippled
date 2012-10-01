#include "TransactionFormats.h"

#define TF_BASE									\
		{ 	sfTransactionType,	SOE_REQUIRED },	\
		{	sfFlags,			SOE_REQUIRED },	\
		{	sfSourceTag,		SOE_OPTIONAL }, \
		{   sfAccount,			SOE_REQUIRED }, \
		{	sfSequence,			SOE_REQUIRED }, \
		{	sfFee,				SOE_REQUIRED }, \
		{	sfSigningPubKey,	SOE_REQUIRED }, \
		{	sfTxnSignature,		SOE_OPTIONAL },

TransactionFormat	InnerTxnFormats[]=
{
	{	"AccountSet", ttACCOUNT_SET, { TF_BASE
		{	sfEmailHash,		SOE_OPTIONAL },
		{	sfWalletLocator,	SOE_OPTIONAL },
		{	sfMessageKey,		SOE_OPTIONAL },
		{	sfDomain,			SOE_OPTIONAL },
		{	sfTransferRate,		SOE_OPTIONAL },
		{	sfPublishHash,		SOE_OPTIONAL },
		{	sfPublishSize,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"Claim", ttCLAIM, { TF_BASE
		{	sfGenerator,		SOE_REQUIRED },
		{	sfPublicKey,		SOE_REQUIRED },
		{	sfSignature,		SOE_REQUIRED },
		{	sfInvalid,			SOE_END } }
	 },
	{	"CreditSet", ttCREDIT_SET, { TF_BASE
		{	sfLimitAmount,		SOE_OPTIONAL },
		{	sfQualityIn,		SOE_OPTIONAL },
		{	sfQualityOut,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
		/*
	{	"Invoice", ttINVOICE, { TF_BASE
		{	sfTarget,			SOE_REQUIRED },
		{	sfAmount,			SOE_REQUIRED },
		{	sfDestination,		SOE_OPTIONAL },
		{	sfIdentifier,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	*/
	{	"NicknameSet", ttNICKNAME_SET, { TF_BASE
		{	sfNickname,			SOE_REQUIRED },
		{	sfMinimumOffer,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"OfferCreate", ttOFFER_CREATE, { TF_BASE
		{	sfTakerPays,		SOE_REQUIRED },
		{	sfTakerGets,		SOE_REQUIRED },
		{	sfExpiration,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"OfferCancel", ttOFFER_CANCEL, { TF_BASE
		{	sfOfferSequence,	SOE_REQUIRED },
		{	sfInvalid,			SOE_END } }
	 },
	{	"PasswordFund", ttPASSWORD_FUND, { TF_BASE
		{	sfDestination,		SOE_REQUIRED },
		{	sfInvalid,			SOE_END } }
	 },
	{	"PasswordSet", ttPASSWORD_SET, { TF_BASE
		{	sfAuthorizedKey,	SOE_REQUIRED },
		{	sfGenerator,		SOE_REQUIRED },
		{	sfPublicKey,		SOE_REQUIRED },
		{	sfInvalid,			SOE_END } }
	 },
	{	"Payment", ttPAYMENT, { TF_BASE
		{	sfDestination,		SOE_REQUIRED },
		{	sfAmount,			SOE_REQUIRED },
		{	sfSendMax,			SOE_OPTIONAL },
		{	sfPaths,			SOE_OPTIONAL },
		{	sfInvoiceID,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"WalletAdd", ttWALLET_ADD, { TF_BASE
		{	sfAmount,			SOE_REQUIRED },
		{	sfAuthorizedKey,	SOE_REQUIRED },
		{	sfPublicKey,		SOE_REQUIRED },
		{	sfInvalid,			SOE_END } }
	 },
	{	"Contract", ttCONTRACT, { TF_BASE
		{	sfExpiration,		SOE_REQUIRED },
		{	sfBondAmount,		SOE_REQUIRED },
		{	sfStampEscrow,		SOE_REQUIRED },
		{	sfRippleEscrow,		SOE_REQUIRED },
		{	sfCreateCode,		SOE_OPTIONAL },
		{	sfFundCode,			SOE_OPTIONAL },
		{	sfRemoveCode,		SOE_OPTIONAL },
		{	sfExpireCode,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"RemoveContract", ttCONTRACT_REMOVE, { TF_BASE
		{	sfTarget,			SOE_REQUIRED },
		{	sfInvalid,			SOE_END } }
	 },
	{ NULL, ttINVALID }
};

TransactionFormat*	getTxnFormat(TransactionType	t)
{
	TransactionFormat*	f	=	InnerTxnFormats;
	while	(f->t_name	!=	NULL)
	{
		if	(f->t_type	==	t)	return	f;
		++f;
 }
	return	NULL;
}
//	vim:ts=4
