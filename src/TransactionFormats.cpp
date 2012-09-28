
#include	"TransactionFormats.h"

#define TF_BASE									\
		{ 	sfTransactionType,	SOE_REQUIRED },	\
		{	sfFlags,			SOE_REQUIRED },	\
		{	sfSignature,		SOE_OPTIONAL },

TransactionFormat	InnerTxnFormats[]=
{
	{	"AccountSet", ttACCOUNT_SET, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
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
		{	sfFlags,			SOE_REQUIRED },
		{	sfGenerator,		SOE_REQUIRED },
		{	sfPublicKey,		SOE_REQUIRED },
		{	sfSignature,		SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"CreditSet", ttCREDIT_SET, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfDestination,		SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfLimitAmount,		SOE_OPTIONAL },
		{	sfQualityIn,		SOE_OPTIONAL },
		{	sfQualityOut,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
		/*
	{	"Invoice", ttINVOICE, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfTarget,			SOE_REQUIRED },
		{	sfAmount,			SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfDestination,		SOE_OPTIONAL },
		{	sfIdentifier,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	*/
	{	"NicknameSet", ttNICKNAME_SET, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfNickname,			SOE_REQUIRED },
		{	sfMinimumOffer,		SOE_OPTIONAL },
		{	sfSignature,		SOE_OPTIONAL },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"OfferCreate", ttOFFER_CREATE, { TF_BASE
		{	sfFlags,			SOE_REQUIRED},
		{	sfTakerPays,		SOE_REQUIRED },
		{	sfTakerGets,		SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfExpiration,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"OfferCancel", ttOFFER_CANCEL, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfOfferSequence,	SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"PasswordFund", ttPASSWORD_FUND, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfDestination,		SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"PasswordSet", ttPASSWORD_SET, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfAuthorizedKey,	SOE_REQUIRED },
		{	sfGenerator,		SOE_REQUIRED },
		{	sfPublicKey,		SOE_REQUIRED },
		{	sfSignature,		SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"Payment", ttPAYMENT, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfDestination,		SOE_REQUIRED },
		{	sfAmount,			SOE_REQUIRED },
		{	sfSendMax,			SOE_OPTIONAL },
		{	sfPaths,			SOE_OPTIONAL },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvoiceID,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"WalletAdd", ttWALLET_ADD, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
		{	sfAmount,			SOE_REQUIRED },
		{	sfAuthorizedKey,	SOE_REQUIRED },
		{	sfPublicKey,		SOE_REQUIRED },
		{	sfSignature,		SOE_REQUIRED },
		{	sfSourceTag,		SOE_OPTIONAL },
		{	sfInvalid,			SOE_END } }
	 },
	{	"Contract", ttCONTRACT, { TF_BASE
		{	sfFlags,			SOE_REQUIRED },
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
		{	sfFlags,			SOE_REQUIRED },
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
