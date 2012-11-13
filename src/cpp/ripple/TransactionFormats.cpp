#include "TransactionFormats.h"

std::map<int, TransactionFormat*> TransactionFormat::byType;
std::map<std::string, TransactionFormat*> TransactionFormat::byName;

#define TF_BASE												\
		<< SOElement(sfTransactionType,		SOE_REQUIRED)	\
		<< SOElement(sfFlags,				SOE_REQUIRED)	\
		<< SOElement(sfSourceTag,			SOE_OPTIONAL)	\
		<< SOElement(sfAccount,				SOE_REQUIRED)	\
		<< SOElement(sfSequence,			SOE_REQUIRED)	\
		<< SOElement(sfFee,					SOE_REQUIRED)	\
		<< SOElement(sfSigningPubKey,		SOE_REQUIRED)	\
		<< SOElement(sfTxnSignature,		SOE_OPTIONAL)

#define DECLARE_TF(name, type) tf = new TransactionFormat(#name, type); (*tf) TF_BASE

static bool TFInit()
{
	TransactionFormat* tf;

	DECLARE_TF(AccountSet, ttACCOUNT_SET)
		<< SOElement(sfEmailHash,		SOE_OPTIONAL)
		<< SOElement(sfWalletLocator,	SOE_OPTIONAL)
		<< SOElement(sfWalletSize,		SOE_OPTIONAL)
		<< SOElement(sfMessageKey,		SOE_OPTIONAL)
		<< SOElement(sfDomain,			SOE_OPTIONAL)
		<< SOElement(sfTransferRate,	SOE_OPTIONAL)
		;

	DECLARE_TF(TrustSet, ttTRUST_SET)
		<< SOElement(sfLimitAmount,		SOE_OPTIONAL)
		<< SOElement(sfQualityIn,		SOE_OPTIONAL)
		<< SOElement(sfQualityOut,		SOE_OPTIONAL)
		;

	DECLARE_TF(OfferCreate, ttOFFER_CREATE)
		<< SOElement(sfTakerPays,		SOE_REQUIRED)
		<< SOElement(sfTakerGets,		SOE_REQUIRED)
		<< SOElement(sfExpiration,		SOE_OPTIONAL)
		;

	DECLARE_TF(OfferCancel, ttOFFER_CANCEL)
		<< SOElement(sfOfferSequence,	SOE_REQUIRED)
		;

	DECLARE_TF(SetRegularKey, ttREGULAR_KEY_SET)
		<< SOElement(sfRegularKey,	SOE_REQUIRED)
		;

	DECLARE_TF(Payment, ttPAYMENT)
		<< SOElement(sfDestination,		SOE_REQUIRED)
		<< SOElement(sfAmount,			SOE_REQUIRED)
		<< SOElement(sfSendMax,			SOE_OPTIONAL)
		<< SOElement(sfPaths,			SOE_OPTIONAL)
		<< SOElement(sfInvoiceID,		SOE_OPTIONAL)
		;

	DECLARE_TF(Contract, ttCONTRACT)
		<< SOElement(sfExpiration,		SOE_REQUIRED)
		<< SOElement(sfBondAmount,		SOE_REQUIRED)
		<< SOElement(sfStampEscrow,		SOE_REQUIRED)
		<< SOElement(sfRippleEscrow,	SOE_REQUIRED)
		<< SOElement(sfCreateCode,		SOE_OPTIONAL)
		<< SOElement(sfFundCode,		SOE_OPTIONAL)
		<< SOElement(sfRemoveCode,		SOE_OPTIONAL)
		<< SOElement(sfExpireCode,		SOE_OPTIONAL)
		;

	DECLARE_TF(RemoveContract, ttCONTRACT_REMOVE)
		<< SOElement(sfTarget,			SOE_REQUIRED)
		;

	return true;
}

bool TFInitComplete = TFInit();

TransactionFormat* TransactionFormat::getTxnFormat(TransactionType t)
{
	std::map<int, TransactionFormat*>::iterator it = byType.find(static_cast<int>(t));
	if (it == byType.end())
		return NULL;
	return it->second;
}

TransactionFormat* TransactionFormat::getTxnFormat(int t)
{
	std::map<int, TransactionFormat*>::iterator it = byType.find((t));
	if (it == byType.end())
		return NULL;
	return it->second;
}

TransactionFormat* TransactionFormat::getTxnFormat(const std::string& t)
{
	std::map<std::string, TransactionFormat*>::iterator it = byName.find((t));
	if (it == byName.end())
		return NULL;
	return it->second;
}

//	vim:ts=4
