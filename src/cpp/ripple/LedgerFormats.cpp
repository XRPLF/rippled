
#include "LedgerFormats.h"

std::map<int, LedgerEntryFormat*> LedgerEntryFormat::byType;
std::map<std::string, LedgerEntryFormat*> LedgerEntryFormat::byName;

#define LEF_BASE											\
	<< SOElement(sfLedgerIndex,				SOE_OPTIONAL)	\
	<< SOElement(sfLedgerEntryType,			SOE_REQUIRED)	\
	<< SOElement(sfFlags,					SOE_REQUIRED)

#define DECLARE_LEF(name, type) lef = new LedgerEntryFormat(#name, type); (*lef) LEF_BASE

static bool LEFInit()
{
	LedgerEntryFormat* lef;

	DECLARE_LEF(AccountRoot, ltACCOUNT_ROOT)
		<< SOElement(sfAccount,				SOE_REQUIRED)
		<< SOElement(sfSequence,			SOE_REQUIRED)
		<< SOElement(sfBalance,				SOE_REQUIRED)
		<< SOElement(sfPreviousTxnID,		SOE_REQUIRED)
		<< SOElement(sfPreviousTxnLgrSeq,	SOE_REQUIRED)
		<< SOElement(sfAuthorizedKey,		SOE_OPTIONAL)
		<< SOElement(sfEmailHash,			SOE_OPTIONAL)
		<< SOElement(sfWalletLocator,		SOE_OPTIONAL)
		<< SOElement(sfWalletSize,			SOE_OPTIONAL)
		<< SOElement(sfMessageKey,			SOE_OPTIONAL)
		<< SOElement(sfTransferRate,		SOE_OPTIONAL)
		<< SOElement(sfDomain,				SOE_OPTIONAL)
		;

	DECLARE_LEF(Contract, ltCONTRACT)
		<< SOElement(sfAccount,				SOE_REQUIRED)
		<< SOElement(sfBalance,				SOE_REQUIRED)
		<< SOElement(sfPreviousTxnID,		SOE_REQUIRED)
		<< SOElement(sfPreviousTxnLgrSeq,	SOE_REQUIRED)
		<< SOElement(sfIssuer,				SOE_REQUIRED)
		<< SOElement(sfOwner,				SOE_REQUIRED)
		<< SOElement(sfExpiration,			SOE_REQUIRED)
		<< SOElement(sfBondAmount,			SOE_REQUIRED)
		<< SOElement(sfCreateCode,			SOE_REQUIRED)
		<< SOElement(sfFundCode,			SOE_REQUIRED)
		<< SOElement(sfRemoveCode,			SOE_REQUIRED)
		<< SOElement(sfExpireCode,			SOE_REQUIRED)
		;

	DECLARE_LEF(DirectoryNode, ltDIR_NODE)
		<< SOElement(sfIndexes,				SOE_REQUIRED)
		<< SOElement(sfIndexNext,			SOE_OPTIONAL)
		<< SOElement(sfIndexPrevious,		SOE_OPTIONAL)
		;

	DECLARE_LEF(GeneratorMap, ltGENERATOR_MAP)
		<< SOElement(sfGenerator,			SOE_REQUIRED)
		;

	DECLARE_LEF(Nickname, ltNICKNAME)
		<< SOElement(sfAccount,				SOE_REQUIRED)
		<< SOElement(sfMinimumOffer,		SOE_OPTIONAL)
		;

	DECLARE_LEF(Offer, ltOFFER)
		<< SOElement(sfAccount,				SOE_REQUIRED)
		<< SOElement(sfSequence,			SOE_REQUIRED)
		<< SOElement(sfTakerPays,			SOE_REQUIRED)
		<< SOElement(sfTakerGets,			SOE_REQUIRED)
		<< SOElement(sfBookDirectory,		SOE_REQUIRED)
		<< SOElement(sfBookNode,			SOE_REQUIRED)
		<< SOElement(sfOwnerNode,			SOE_REQUIRED)
		<< SOElement(sfPreviousTxnID,		SOE_REQUIRED)
		<< SOElement(sfPreviousTxnLgrSeq,	SOE_REQUIRED)
		<< SOElement(sfExpiration,			SOE_OPTIONAL)
		;

	DECLARE_LEF(RippleState, ltRIPPLE_STATE)
		<< SOElement(sfBalance,				SOE_REQUIRED)
		<< SOElement(sfLowLimit,			SOE_REQUIRED)
		<< SOElement(sfHighLimit,			SOE_REQUIRED)
		<< SOElement(sfPreviousTxnID,		SOE_REQUIRED)
		<< SOElement(sfPreviousTxnLgrSeq,	SOE_REQUIRED)
		<< SOElement(sfLowQualityIn,		SOE_OPTIONAL)
		<< SOElement(sfLowQualityOut,		SOE_OPTIONAL)
		<< SOElement(sfHighQualityIn,		SOE_OPTIONAL)
		<< SOElement(sfHighQualityOut,		SOE_OPTIONAL)
		;

	DECLARE_LEF(LedgerHashes, ltLEDGER_HASHES)
		<< SOElement(sfFirstLedgerSequence,	SOE_OPTIONAL)
		<< SOElement(sfLastLedgerSequence,	SOE_OPTIONAL)
		<< SOElement(sfHashes,				SOE_REQUIRED)
		;

		return true;
}

bool LEFInitComplete = LEFInit();

LedgerEntryFormat* LedgerEntryFormat::getLgrFormat(LedgerEntryType t)
{
	std::map<int, LedgerEntryFormat*>::iterator it = byType.find(static_cast<int>(t));
	if (it == byType.end())
		return NULL;
	return it->second;
}

LedgerEntryFormat* LedgerEntryFormat::getLgrFormat(int t)
{
	std::map<int, LedgerEntryFormat*>::iterator it = byType.find((t));
	if (it == byType.end())
		return NULL;
	return it->second;
}

LedgerEntryFormat* LedgerEntryFormat::getLgrFormat(const std::string& t)
{
	std::map<std::string, LedgerEntryFormat*>::iterator it = byName.find((t));
	if (it == byName.end())
		return NULL;
	return it->second;
}

// vim:ts=4
