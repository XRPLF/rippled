//
// XXX Should make sure all fields and are recognized on a transactions.
//

#include "TransactionEngine.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <queue>

#include "../json/writer.h"

#include "Config.h"
#include "Log.h"
#include "TransactionFormats.h"
#include "utils.h"
#include "Interpreter.h"
#include "Contract.h"

// Small for testing, should likely be 32 or 64.
#define DIR_NODE_MAX		2
#define RIPPLE_PATHS_MAX	3

static STAmount saZero(CURRENCY_ONE, ACCOUNT_ONE, 0);
static STAmount saOne(CURRENCY_ONE, ACCOUNT_ONE, 1);

std::size_t hash_value(const aciSource& asValue)
{
	std::size_t seed = 0;

	asValue.get<0>().hash_combine(seed);
	asValue.get<1>().hash_combine(seed);
	asValue.get<2>().hash_combine(seed);

	return seed;
}

bool transResultInfo(TER terCode, std::string& strToken, std::string& strHuman)
{
	static struct {
		TER				terCode;
		const char*		cpToken;
		const char*		cpHuman;
	} transResultInfoA[] = {
		{	tefALREADY,				"tefALREADY",				"The exact transaction was already in this ledger"	},
		{	tefBAD_ADD_AUTH,		"tefBAD_ADD_AUTH",			"Not authorized to add account."					},
		{	tefBAD_AUTH,			"tefBAD_AUTH",				"Transaction's public key is not authorized."		},
		{	tefBAD_CLAIM_ID,		"tefBAD_CLAIM_ID",			"Malformed."										},
		{	tefBAD_GEN_AUTH,		"tefBAD_GEN_AUTH",			"Not authorized to claim generator."				},
		{	tefBAD_LEDGER,			"tefBAD_LEDGER",			"Ledger in unexpected state."						},
		{	tefCLAIMED,				"tefCLAIMED",				"Can not claim a previously claimed account."		},
		{	tefEXCEPTION,			"tefEXCEPTION",				"Unexpected program state."							},
		{	tefCREATED,				"tefCREATED",				"Can't add an already created account."				},
		{	tefGEN_IN_USE,			"tefGEN_IN_USE",			"Generator already in use."							},
		{	tefPAST_SEQ,			"tefPAST_SEQ",				"This sequence number has already past"				},

		{	telBAD_PATH_COUNT,		"telBAD_PATH_COUNT",		"Malformed: too many paths."						},
		{	telINSUF_FEE_P,			"telINSUF_FEE_P",			"Fee insufficient."									},

		{	temBAD_AMOUNT,			"temBAD_AMOUNT",			"Can only send positive amounts."					},
		{	temBAD_AUTH_MASTER,		"temBAD_AUTH_MASTER",		"Auth for unclaimed account needs correct master key."	},
		{	temBAD_EXPIRATION,		"temBAD_EXPIRATION",		"Malformed."										},
		{	temBAD_ISSUER,			"temBAD_ISSUER",			"Malformed."										},
		{	temBAD_OFFER,			"temBAD_OFFER",				"Malformed."										},
		{	temBAD_PATH,			"temBAD_PATH",				"Malformed."										},
		{	temBAD_PATH_LOOP,		"temBAD_PATH_LOOP",			"Malformed."										},
		{	temBAD_PUBLISH,			"temBAD_PUBLISH",			"Malformed: bad publish."							},
		{	temBAD_SET_ID,			"temBAD_SET_ID",			"Malformed."										},
		{	temCREATEXNS,			"temCREATEXNS",				"Can not specify non XNS for Create."				},
		{	temDST_IS_SRC,			"temDST_IS_SRC",			"Destination may not be source."					},
		{	temDST_NEEDED,			"temDST_NEEDED",			"Destination not specified."						},
		{	temINSUF_FEE_P,			"temINSUF_FEE_P",			"Fee not allowed."									},
		{	temINVALID,				"temINVALID",				"The transaction is ill-formed"						},
		{	temREDUNDANT,			"temREDUNDANT",				"Sends same currency to self."						},
		{	temRIPPLE_EMPTY,		"temRIPPLE_EMPTY",			"PathSet with no paths."							},
		{	temUNCERTAIN,			"temUNCERTAIN",				"In process of determining result. Never returned."		},
		{	temUNKNOWN,				"temUNKNOWN",				"The transactions requires logic not implemented yet."	},

		{	tepPATH_DRY,			"tepPATH_DRY",				"Path could not send partial amount."				},
		{	tepPATH_PARTIAL,		"tepPATH_PARTIAL",			"Path could not send full amount."					},

		{	terDIR_FULL,			"terDIR_FULL",				"Can not add entry to full dir."					},
		{	terFUNDS_SPENT,			"terFUNDS_SPENT",			"Can't set password, password set funds already spent."	},
		{	terINSUF_FEE_B,			"terINSUF_FEE_B",			"Account balance can't pay fee."					},
		{	terNO_ACCOUNT,			"terNO_ACCOUNT",			"The source account does not exist."				},
		{	terNO_DST,				"terNO_DST",				"The destination does not exist"					},
		{	terNO_LINE,				"terNO_LINE",				"No such line."										},
		{	terNO_LINE_NO_ZERO,		"terNO_LINE_NO_ZERO",		"Can't zero non-existant line, destination might make it."	},
		{	terOFFER_NOT_FOUND,		"terOFFER_NOT_FOUND",		"Can not cancel offer."								},
		{	terPRE_SEQ,				"terPRE_SEQ",				"Missing/inapplicable prior transaction"			},
		{	terSET_MISSING_DST,		"terSET_MISSING_DST",		"Can't set password, destination missing."			},
		{	terUNFUNDED,			"terUNFUNDED",				"Source account had insufficient balance for transaction."	},

		{	tesSUCCESS,				"tesSUCCESS",				"The transaction was applied"						},
	};

	int	iIndex	= NUMBER(transResultInfoA);

	while (iIndex-- && transResultInfoA[iIndex].terCode != terCode)
		;

	if (iIndex >= 0)
	{
		strToken	= transResultInfoA[iIndex].cpToken;
		strHuman	= transResultInfoA[iIndex].cpHuman;
	}

	return iIndex >= 0;
}

static std::string transToken(TER terCode)
{
	std::string	strToken;
	std::string	strHuman;

	return transResultInfo(terCode, strToken, strHuman) ? strToken : "-";
}

#if 0
static std::string transHuman(TER terCode)
{
	std::string	strToken;
	std::string	strHuman;

	return transResultInfo(terCode, strToken, strHuman) ? strHuman : "-";
}
#endif

// Returns amount owed by uToAccountID to uFromAccountID.
// <-- $owed/uCurrencyID/uToAccountID: positive: uFromAccountID holds IOUs., negative: uFromAccountID owes IOUs.
STAmount TransactionEngine::rippleOwed(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saBalance;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	if (sleRippleState)
	{
		saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);
		if (uToAccountID < uFromAccountID)
			saBalance.negate();
		saBalance.setIssuer(uToAccountID);
	}
	else
	{
		Log(lsINFO) << "rippleOwed: No credit line between "
			<< NewcoinAddress::createHumanAccountID(uFromAccountID)
			<< " and "
			<< NewcoinAddress::createHumanAccountID(uToAccountID)
			<< " for "
			<< STAmount::createHumanCurrency(uCurrencyID)
			<< "." ;

		assert(false);
	}

	return saBalance;

}

// Maximum amount of IOUs uToAccountID will hold from uFromAccountID.
// <-- $amount/uCurrencyID/uToAccountID.
STAmount TransactionEngine::rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saLimit;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	assert(sleRippleState);
	if (sleRippleState)
	{
		saLimit	= sleRippleState->getIValueFieldAmount(uToAccountID < uFromAccountID ? sfLowLimit : sfHighLimit);
		saLimit.setIssuer(uToAccountID);
	}

	return saLimit;

}

uint32 TransactionEngine::rippleTransferRate(const uint160& uIssuerID)
{
	SLE::pointer	sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uIssuerID));

	uint32			uQuality	= sleAccount && sleAccount->getIFieldPresent(sfTransferRate)
									? sleAccount->getIFieldU32(sfTransferRate)
									: QUALITY_ONE;

	Log(lsINFO) << boost::str(boost::format("rippleTransferRate: uIssuerID=%s account_exists=%d transfer_rate=%f")
		% NewcoinAddress::createHumanAccountID(uIssuerID)
		% !!sleAccount
		% (uQuality/1000000000.0));

	assert(sleAccount);

	return uQuality;
}

// XXX Might not need this, might store in nodes on calc reverse.
uint32 TransactionEngine::rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID, const SOE_Field sfLow, const SOE_Field sfHigh)
{
	uint32			uQuality		= QUALITY_ONE;
	SLE::pointer	sleRippleState;

	if (uToAccountID == uFromAccountID)
	{
		nothing();
	}
	else
	{
		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

		if (sleRippleState)
		{
			SOE_Field	sfField	= uToAccountID < uFromAccountID ? sfLow: sfHigh;

			uQuality	= sleRippleState->getIFieldPresent(sfField)
							? sleRippleState->getIFieldU32(sfField)
							: QUALITY_ONE;

			if (!uQuality)
				uQuality	= 1;	// Avoid divide by zero.
		}
	}

	Log(lsINFO) << boost::str(boost::format("rippleQuality: %s uToAccountID=%s uFromAccountID=%s uCurrencyID=%s bLine=%d uQuality=%f")
		% (sfLow == sfLowQualityIn ? "in" : "out")
		% NewcoinAddress::createHumanAccountID(uToAccountID)
		% NewcoinAddress::createHumanAccountID(uFromAccountID)
		% STAmount::createHumanCurrency(uCurrencyID)
		% !!sleRippleState
		% (uQuality/1000000000.0));

	assert(uToAccountID == uFromAccountID || !!sleRippleState);

	return uQuality;
}

// Return how much of uIssuerID's uCurrencyID IOUs that uAccountID holds.  May be negative.
// <-- IOU's uAccountID has of uIssuerID
STAmount TransactionEngine::rippleHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount			saBalance;
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uAccountID, uIssuerID, uCurrencyID));

	if (sleRippleState)
	{
		saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);

		if (uAccountID > uIssuerID)
			saBalance.negate();		// Put balance in uAccountID terms.
	}

	return saBalance;
}

// <-- saAmount: amount of uCurrencyID held by uAccountID. May be negative.
STAmount TransactionEngine::accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount	saAmount;

	if (!uCurrencyID)
	{
		SLE::pointer		sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uAccountID));

		saAmount	= sleAccount->getIValueFieldAmount(sfBalance);

		Log(lsINFO) << "accountHolds: stamps: " << saAmount.getText();
	}
	else
	{
		saAmount	= rippleHolds(uAccountID, uCurrencyID, uIssuerID);

		Log(lsINFO) << "accountHolds: "
			<< saAmount.getFullText()
			<< " : "
			<< STAmount::createHumanCurrency(uCurrencyID)
			<< "/"
			<< NewcoinAddress::createHumanAccountID(uIssuerID);
	}

	return saAmount;
}

// Returns the funds available for uAccountID for a currency/issuer.
// Use when you need a default for rippling uAccountID's currency.
// --> saDefault/currency/issuer
// <-- saFunds: Funds available. May be negative.
//              If the issuer is the same as uAccountID, funds are unlimited, use result is saDefault.
STAmount TransactionEngine::accountFunds(const uint160& uAccountID, const STAmount& saDefault)
{
	STAmount	saFunds;

	Log(lsINFO) << "accountFunds: uAccountID="
			<< NewcoinAddress::createHumanAccountID(uAccountID);
	Log(lsINFO) << "accountFunds: saDefault.isNative()=" << saDefault.isNative();
	Log(lsINFO) << "accountFunds: saDefault.getIssuer()="
			<< NewcoinAddress::createHumanAccountID(saDefault.getIssuer());

	if (!saDefault.isNative() && saDefault.getIssuer() == uAccountID)
	{
		saFunds	= saDefault;

		Log(lsINFO) << "accountFunds: offer funds: ripple self-funded: " << saFunds.getText();
	}
	else
	{
		saFunds	= accountHolds(uAccountID, saDefault.getCurrency(), saDefault.getIssuer());

		Log(lsINFO) << "accountFunds: offer funds: uAccountID ="
			<< NewcoinAddress::createHumanAccountID(uAccountID)
			<< " : "
			<< saFunds.getText()
			<< "/"
			<< saDefault.getHumanCurrency()
			<< "/"
			<< NewcoinAddress::createHumanAccountID(saDefault.getIssuer());
	}

	return saFunds;
}

// Calculate transit fee.
STAmount TransactionEngine::rippleTransferFee(const uint160& uSenderID, const uint160& uReceiverID, const uint160& uIssuerID, const STAmount& saAmount)
{
	STAmount	saTransitFee;

	if (uSenderID != uIssuerID && uReceiverID != uIssuerID)
	{
		uint32		uTransitRate	= rippleTransferRate(uIssuerID);

		if (QUALITY_ONE != uTransitRate)
		{
			STAmount		saTransitRate(CURRENCY_ONE, uTransitRate, -9);

			saTransitFee	= STAmount::multiply(saAmount, saTransitRate, saAmount.getCurrency(), saAmount.getIssuer());
		}
	}

	return saTransitFee;
}

// Direct send w/o fees: redeeming IOUs and/or sending own IOUs.
void TransactionEngine::rippleCredit(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount, bool bCheckIssuer)
{
	uint160				uIssuerID		= saAmount.getIssuer();

	assert(!bCheckIssuer || uSenderID == uIssuerID || uReceiverID == uIssuerID);

	bool				bFlipped		= uSenderID > uReceiverID;
	uint256				uIndex			= Ledger::getRippleStateIndex(uSenderID, uReceiverID, saAmount.getCurrency());
	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, uIndex);

	if (!sleRippleState)
	{
		Log(lsINFO) << "rippleCredit: Creating ripple line: " << uIndex.ToString();

		STAmount	saBalance	= saAmount;

		sleRippleState	= entryCreate(ltRIPPLE_STATE, uIndex);

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setIFieldAmount(sfBalance, saBalance);
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, uSenderID);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uReceiverID);
	}
	else
	{
		STAmount	saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);

		if (!bFlipped)
			saBalance.negate();		// Put balance in low terms.

		saBalance	+= saAmount;

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setIFieldAmount(sfBalance, saBalance);

		entryModify(sleRippleState);
	}
}

// Send regardless of limits.
// --> saAmount: Amount/currency/issuer for receiver to get.
// <-- saActual: Amount actually sent.  Sender pay's fees.
STAmount TransactionEngine::rippleSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	STAmount		saActual;
	const uint160	uIssuerID	= saAmount.getIssuer();

	assert(!!uSenderID && !!uReceiverID);

	if (uSenderID == uIssuerID || uReceiverID == uIssuerID)
	{
		// Direct send: redeeming IOUs and/or sending own IOUs.
		rippleCredit(uSenderID, uReceiverID, saAmount);

		saActual	= saAmount;
	}
	else
	{
		// Sending 3rd party IOUs: transit.

		STAmount		saTransitFee	= rippleTransferFee(uSenderID, uReceiverID, uIssuerID, saAmount);

		saActual	= !saTransitFee ? saAmount : saAmount+saTransitFee;

		saActual.setIssuer(uIssuerID);	// XXX Make sure this done in + above.

		rippleCredit(uIssuerID, uReceiverID, saAmount);
		rippleCredit(uSenderID, uIssuerID, saActual);
	}

	return saActual;
}

void TransactionEngine::accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	assert(!saAmount.isNegative());

	if (!saAmount)
	{
		nothing();
	}
	else if (saAmount.isNative())
	{
		SLE::pointer		sleSender	= !!uSenderID
											? entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uSenderID))
											: SLE::pointer();
		SLE::pointer		sleReceiver	= !!uReceiverID
											? entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uReceiverID))
											: SLE::pointer();

		Log(lsINFO) << boost::str(boost::format("accountSend> %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender ? (sleSender->getIValueFieldAmount(sfBalance)).getFullText() : "-")
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver ? (sleReceiver->getIValueFieldAmount(sfBalance)).getFullText() : "-")
			% saAmount.getFullText());

		if (sleSender)
		{
			sleSender->setIFieldAmount(sfBalance, sleSender->getIValueFieldAmount(sfBalance) - saAmount);
			entryModify(sleSender);
		}

		if (sleReceiver)
		{
			sleReceiver->setIFieldAmount(sfBalance, sleReceiver->getIValueFieldAmount(sfBalance) + saAmount);
			entryModify(sleReceiver);
		}

		Log(lsINFO) << boost::str(boost::format("accountSend< %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender ? (sleSender->getIValueFieldAmount(sfBalance)).getFullText() : "-")
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver ? (sleReceiver->getIValueFieldAmount(sfBalance)).getFullText() : "-")
			% saAmount.getFullText());
	}
	else
	{
		rippleSend(uSenderID, uReceiverID, saAmount);
	}
}

TER TransactionEngine::offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID)
{
	uint64	uOwnerNode	= sleOffer->getIFieldU64(sfOwnerNode);
	TER		terResult	= dirDelete(false, uOwnerNode, Ledger::getOwnerDirIndex(uOwnerID), uOfferIndex, false);

	if (tesSUCCESS == terResult)
	{
		uint256		uDirectory	= sleOffer->getIFieldH256(sfBookDirectory);
		uint64		uBookNode	= sleOffer->getIFieldU64(sfBookNode);

		terResult	= dirDelete(false, uBookNode, uDirectory, uOfferIndex, true);
	}

	entryDelete(sleOffer);

	return terResult;
}

TER TransactionEngine::offerDelete(const uint256& uOfferIndex)
{
	SLE::pointer	sleOffer	= entryCache(ltOFFER, uOfferIndex);
	const uint160	uOwnerID	= sleOffer->getIValueFieldAccount(sfAccount).getAccountID();

	return offerDelete(sleOffer, uOfferIndex, uOwnerID);
}

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// We only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TER TransactionEngine::dirAdd(
	uint64&							uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	SLE::pointer		sleNode;
	STVector256			svIndexes;
	SLE::pointer		sleRoot		= entryCache(ltDIR_NODE, uRootIndex);

	if (!sleRoot)
	{
		// No root, make it.
		sleRoot		= entryCreate(ltDIR_NODE, uRootIndex);

		sleNode		= sleRoot;
		uNodeDir	= 0;
	}
	else
	{
		uNodeDir	= sleRoot->getIFieldU64(sfIndexPrevious);		// Get index to last directory node.

		if (uNodeDir)
		{
			// Try adding to last node.
			sleNode		= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));

			assert(sleNode);
		}
		else
		{
			// Try adding to root.  Didn't have a previous set to the last node.
			sleNode		= sleRoot;
		}

		svIndexes	= sleNode->getIFieldV256(sfIndexes);

		if (DIR_NODE_MAX != svIndexes.peekValue().size())
		{
			// Add to current node.
			entryModify(sleNode);
		}
		// Add to new node.
		else if (!++uNodeDir)
		{
			return terDIR_FULL;
		}
		else
		{
			// Have old last point to new node, if it was not root.
			if (uNodeDir == 1)
			{
				// Previous node is root node.

				sleRoot->setIFieldU64(sfIndexNext, uNodeDir);
			}
			else
			{
				// Previous node is not root node.

				SLE::pointer	slePrevious	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir-1));

				slePrevious->setIFieldU64(sfIndexNext, uNodeDir);
				entryModify(slePrevious);

				sleNode->setIFieldU64(sfIndexPrevious, uNodeDir-1);
			}

			// Have root point to new node.
			sleRoot->setIFieldU64(sfIndexPrevious, uNodeDir);
			entryModify(sleRoot);

			// Create the new node.
			sleNode		= entryCreate(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));
			svIndexes	= STVector256();
		}
	}

	svIndexes.peekValue().push_back(uLedgerIndex);	// Append entry.
	sleNode->setIFieldV256(sfIndexes, svIndexes);	// Save entry.

	Log(lsINFO) << "dirAdd:   creating: root: " << uRootIndex.ToString();
	Log(lsINFO) << "dirAdd:  appending: Entry: " << uLedgerIndex.ToString();
	Log(lsINFO) << "dirAdd:  appending: Node: " << strHex(uNodeDir);
	// Log(lsINFO) << "dirAdd:  appending: PREV: " << svIndexes.peekValue()[0].ToString();

	return tesSUCCESS;
}

// Ledger must be in a state for this to work.
TER TransactionEngine::dirDelete(
	const bool						bKeepRoot,		// --> True, if we never completely clean up, after we overflow the root node.
	const uint64&					uNodeDir,		// --> Node containing entry.
	const uint256&					uRootIndex,		// --> The index of the base of the directory.  Nodes are based off of this.
	const uint256&					uLedgerIndex,	// --> Value to add to directory.
	const bool						bStable)		// --> True, not to change relative order of entries.
{
	uint64				uNodeCur	= uNodeDir;
	SLE::pointer		sleNode		= entryCache(ltDIR_NODE, uNodeCur ? Ledger::getDirNodeIndex(uRootIndex, uNodeCur) : uRootIndex);

	assert(sleNode);

	if (!sleNode)
	{
		Log(lsWARNING) << "dirDelete: no such node";

		return tefBAD_LEDGER;
	}

	STVector256						svIndexes	= sleNode->getIFieldV256(sfIndexes);
	std::vector<uint256>&			vuiIndexes	= svIndexes.peekValue();
	std::vector<uint256>::iterator	it;

	it = std::find(vuiIndexes.begin(), vuiIndexes.end(), uLedgerIndex);

	assert(vuiIndexes.end() != it);
	if (vuiIndexes.end() == it)
	{
		assert(false);

		Log(lsWARNING) << "dirDelete: no such entry";

		return tefBAD_LEDGER;
	}

	// Remove the element.
	if (vuiIndexes.size() > 1)
	{
		if (bStable)
		{
			vuiIndexes.erase(it);
		}
		else
		{
			*it = vuiIndexes[vuiIndexes.size()-1];
			vuiIndexes.resize(vuiIndexes.size()-1);
		}
	}
	else
	{
		vuiIndexes.clear();
	}

	sleNode->setIFieldV256(sfIndexes, svIndexes);
	entryModify(sleNode);

	if (vuiIndexes.empty())
	{
		// May be able to delete nodes.
		uint64				uNodePrevious	= sleNode->getIFieldU64(sfIndexPrevious);
		uint64				uNodeNext		= sleNode->getIFieldU64(sfIndexNext);

		if (!uNodeCur)
		{
			// Just emptied root node.

			if (!uNodePrevious)
			{
				// Never overflowed the root node.  Delete it.
				entryDelete(sleNode);
			}
			// Root overflowed.
			else if (bKeepRoot)
			{
				// If root overflowed and not allowed to delete overflowed root node.

				nothing();
			}
			else if (uNodePrevious != uNodeNext)
			{
				// Have more than 2 nodes.  Can't delete root node.

				nothing();
			}
			else
			{
				// Have only a root node and a last node.
				SLE::pointer		sleLast	= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));

				assert(sleLast);

				if (sleLast->getIFieldV256(sfIndexes).peekValue().empty())
				{
					// Both nodes are empty.

					entryDelete(sleNode);	// Delete root.
					entryDelete(sleLast);	// Delete last.
				}
				else
				{
					// Have an entry, can't delete root node.

					nothing();
				}
			}
		}
		// Just emptied a non-root node.
		else if (uNodeNext)
		{
			// Not root and not last node. Can delete node.

			SLE::pointer		slePrevious	= entryCache(ltDIR_NODE, uNodePrevious ? Ledger::getDirNodeIndex(uRootIndex, uNodePrevious) : uRootIndex);

			assert(slePrevious);

			SLE::pointer		sleNext		= entryCache(ltDIR_NODE, uNodeNext ? Ledger::getDirNodeIndex(uRootIndex, uNodeNext) : uRootIndex);

			assert(slePrevious);
			assert(sleNext);

			if (!slePrevious)
			{
				Log(lsWARNING) << "dirDelete: previous node is missing";

				return tefBAD_LEDGER;
			}

			if (!sleNext)
			{
				Log(lsWARNING) << "dirDelete: next node is missing";

				return tefBAD_LEDGER;
			}

			// Fix previous to point to its new next.
			slePrevious->setIFieldU64(sfIndexNext, uNodeNext);
			entryModify(slePrevious);

			// Fix next to point to its new previous.
			sleNext->setIFieldU64(sfIndexPrevious, uNodePrevious);
			entryModify(sleNext);
		}
		// Last node.
		else if (bKeepRoot || uNodePrevious)
		{
			// Not allowed to delete last node as root was overflowed.
			// Or, have pervious entries preventing complete delete.

			nothing();
		}
		else
		{
			// Last and only node besides the root.
			SLE::pointer			sleRoot	= entryCache(ltDIR_NODE, uRootIndex);

			assert(sleRoot);

			if (sleRoot->getIFieldV256(sfIndexes).peekValue().empty())
			{
				// Both nodes are empty.

				entryDelete(sleRoot);	// Delete root.
				entryDelete(sleNode);	// Delete last.
			}
			else
			{
				// Root has an entry, can't delete.

				nothing();
			}
		}
	}

	return tesSUCCESS;
}

// Return the first entry and advance uDirEntry.
// <-- true, if had a next entry.
bool TransactionEngine::dirFirst(
	const uint256& uRootIndex,	// --> Root of directory.
	SLE::pointer& sleNode,		// <-- current node
	unsigned int& uDirEntry,	// <-- next entry
	uint256& uEntryIndex)		// <-- The entry, if available. Otherwise, zero.
{
	sleNode		= entryCache(ltDIR_NODE, uRootIndex);
	uDirEntry	= 0;

	assert(sleNode);			// We never probe for directories.

	return TransactionEngine::dirNext(uRootIndex, sleNode, uDirEntry, uEntryIndex);
}

// Return the current entry and advance uDirEntry.
// <-- true, if had a next entry.
bool TransactionEngine::dirNext(
	const uint256& uRootIndex,	// --> Root of directory
	SLE::pointer& sleNode,		// <-> current node
	unsigned int& uDirEntry,	// <-> next entry
	uint256& uEntryIndex)		// <-- The entry, if available. Otherwise, zero.
{
	STVector256				svIndexes	= sleNode->getIFieldV256(sfIndexes);
	std::vector<uint256>&	vuiIndexes	= svIndexes.peekValue();

	if (uDirEntry == vuiIndexes.size())
	{
		uint64				uNodeNext	= sleNode->getIFieldU64(sfIndexNext);

		if (!uNodeNext)
		{
			uEntryIndex.zero();

			return false;
		}
		else
		{
			sleNode		= entryCache(ltDIR_NODE, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));
			uDirEntry	= 0;

			return dirNext(uRootIndex, sleNode, uDirEntry, uEntryIndex);
		}
	}

	uEntryIndex	= vuiIndexes[uDirEntry++];
Log(lsINFO) << boost::str(boost::format("dirNext: uDirEntry=%d uEntryIndex=%s") % uDirEntry % uEntryIndex);

	return true;
}

// Set the authorized public key for an account.  May also set the generator map.
TER	TransactionEngine::setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator)
{
	//
	// Verify that submitter knows the private key for the generator.
	// Otherwise, people could deny access to generators.
	//

	std::vector<unsigned char>	vucCipher		= txn.getITFieldVL(sfGenerator);
	std::vector<unsigned char>	vucPubKey		= txn.getITFieldVL(sfPubKey);
	std::vector<unsigned char>	vucSignature	= txn.getITFieldVL(sfSignature);
	NewcoinAddress				naAccountPublic	= NewcoinAddress::createAccountPublic(vucPubKey);

	if (!naAccountPublic.accountPublicVerify(Serializer::getSHA512Half(vucCipher), vucSignature))
	{
		Log(lsWARNING) << "createGenerator: bad signature unauthorized generator claim";

		return tefBAD_GEN_AUTH;
	}

	// Create generator.
	uint160				hGeneratorID	= naAccountPublic.getAccountID();

	SLE::pointer		sleGen			= entryCache(ltGENERATOR_MAP, Ledger::getGeneratorIndex(hGeneratorID));
	if (!sleGen)
	{
		// Create the generator.
		Log(lsTRACE) << "createGenerator: creating generator";

		sleGen			= entryCreate(ltGENERATOR_MAP, Ledger::getGeneratorIndex(hGeneratorID));

		sleGen->setIFieldVL(sfGenerator, vucCipher);
	}
	else if (bMustSetGenerator)
	{
		// Doing a claim.  Must set generator.
		// Generator is already in use.  Regular passphrases limited to one wallet.
		Log(lsWARNING) << "createGenerator: generator already in use";

		return tefGEN_IN_USE;
	}

	// Set the public key needed to use the account.
	uint160				uAuthKeyID		= bMustSetGenerator
											? hGeneratorID								// Claim
											: txn.getITFieldAccount(sfAuthorizedKey);	// PasswordSet

	mTxnAccount->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	return tesSUCCESS;
}

void TransactionEngine::txnWrite()
{
	// Write back the account states and add the transaction to the ledger
	for (boost::unordered_map<uint256, LedgerEntrySetEntry>::iterator it = mNodes.begin(), end = mNodes.end();
			it != end; ++it)
	{
		const SLE::pointer&	sleEntry	= it->second.mEntry;

		switch (it->second.mAction)
		{
			case taaNONE:
				assert(false);
				break;

			case taaCACHED:
				break;

			case taaCREATE:
				{
					Log(lsINFO) << "applyTransaction: taaCREATE: " << sleEntry->getText();

					if (mLedger->writeBack(lepCREATE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaMODIFY:
				{
					Log(lsINFO) << "applyTransaction: taaMODIFY: " << sleEntry->getText();

					if (mLedger->writeBack(lepNONE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaDELETE:
				{
					Log(lsINFO) << "applyTransaction: taaDELETE: " << sleEntry->getText();

					if (!mLedger->peekAccountStateMap()->delItem(it->first))
						assert(false);
				}
				break;
		}
	}
}

TER TransactionEngine::applyTransaction(const SerializedTransaction& txn, TransactionEngineParams params)
{
	Log(lsTRACE) << "applyTransaction>";
	assert(mLedger);
	mNodes.init(mLedger, txn.getTransactionID(), mLedger->getLedgerSeq());

#ifdef DEBUG
	if (1)
	{
		Serializer ser;
		txn.add(ser);
		SerializerIterator sit(ser);
		SerializedTransaction s2(sit);
		if (!s2.isEquivalent(txn))
		{
			Log(lsFATAL) << "Transaction serdes mismatch";
			Json::StyledStreamWriter ssw;
			ssw.write(Log(lsINFO).ref(), txn.getJson(0));
			ssw.write(Log(lsFATAL).ref(), s2.getJson(0));
			assert(false);
		}
	}
#endif

	TER		terResult	= tesSUCCESS;
	uint256 txID		= txn.getTransactionID();
	if (!txID)
	{
		Log(lsWARNING) << "applyTransaction: invalid transaction id";

		terResult	= temINVALID;
	}

	//
	// Verify transaction is signed properly.
	//

	// Extract signing key
	// Transactions contain a signing key.  This allows us to trivially verify a transaction has at least been properly signed
	// without going to disk.  Each transaction also notes a source account id.  This is used to verify that the signing key is
	// associated with the account.
	// XXX This could be a lot cleaner to prevent unnecessary copying.
	NewcoinAddress	naSigningPubKey;

	if (tesSUCCESS == terResult)
		naSigningPubKey	= NewcoinAddress::createAccountPublic(txn.peekSigningPubKey());

	// Consistency: really signed.
	if ((tesSUCCESS == terResult) && !isSetBit(params, tapNO_CHECK_SIGN) && !txn.checkSign(naSigningPubKey))
	{
		Log(lsWARNING) << "applyTransaction: Invalid transaction: bad signature";

		terResult	= temINVALID;
	}

	STAmount	saCost		= theConfig.FEE_DEFAULT;

	// Customize behavior based on transaction type.
	if (tesSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
			case ttPASSWORD_SET:
				saCost	= 0;
				break;

			case ttPAYMENT:
				if (txn.getFlags() & tfCreateAccount)
				{
					saCost	= theConfig.FEE_ACCOUNT_CREATE;
				}
				break;

			case ttNICKNAME_SET:
				{
					SLE::pointer		sleNickname		= entryCache(ltNICKNAME, txn.getITFieldH256(sfNickname));

					if (!sleNickname)
						saCost	= theConfig.FEE_NICKNAME_CREATE;
				}
				break;

			case ttACCOUNT_SET:
			case ttCREDIT_SET:
			case ttOFFER_CREATE:
			case ttOFFER_CANCEL:
			case ttPASSWORD_FUND:
			case ttWALLET_ADD:
				nothing();
				break;

			case ttINVALID:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: ttINVALID transaction type";
				terResult = temINVALID;
				break;

			default:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: unknown transaction type";
				terResult = temUNKNOWN;
				break;
		}
	}

	STAmount saPaid = txn.getTransactionFee();

	if (tesSUCCESS == terResult)
	{
		if (saCost)
		{
			// Only check fee is sufficient when the ledger is open.
			if (isSetBit(params, tapOPEN_LEDGER) && saPaid < saCost)
			{
				Log(lsINFO) << "applyTransaction: insufficient fee";

				terResult	= telINSUF_FEE_P;
			}
		}
		else
		{
			if (saPaid)
			{
				// Transaction is malformed.
				Log(lsWARNING) << "applyTransaction: fee not allowed";

				terResult	= temINSUF_FEE_P;
			}
		}
	}

	// Get source account ID.
	mTxnAccountID	= txn.getSourceAccount().getAccountID();
	if (tesSUCCESS == terResult && !mTxnAccountID)
	{
		Log(lsWARNING) << "applyTransaction: bad source id";

		terResult	= temINVALID;
	}

	if (tesSUCCESS != terResult)
		return terResult;

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	mTxnAccount			= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(mTxnAccountID));

	// Find source account
	// If are only forwarding, due to resource limitations, we might verifying only some transactions, this would be probablistic.

	STAmount			saSrcBalance;
	uint32				t_seq			= txn.getSequence();
	bool				bHaveAuthKey	= false;

	if (!mTxnAccount)
	{
		Log(lsTRACE) << boost::str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			txn.getSourceAccount().humanAccountID());

		terResult			= terNO_ACCOUNT;
	}
	else
	{
		saSrcBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);
		bHaveAuthKey	= mTxnAccount->getIFieldPresent(sfAuthorizedKey);
	}

	// Check if account claimed.
	if (tesSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				if (bHaveAuthKey)
				{
					Log(lsWARNING) << "applyTransaction: Account already claimed.";

					terResult	= tefCLAIMED;
				}
				break;

			default:
				nothing();
				break;
		}
	}

	// Consistency: Check signature
	if (tesSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != mTxnAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= tefBAD_CLAIM_ID;
				}
				break;

			case ttPASSWORD_SET:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != mTxnAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= temBAD_SET_ID;
				}
				break;

			default:
				// Verify the transaction's signing public key is the key authorized for signing.
				if (bHaveAuthKey && naSigningPubKey.getAccountID() == mTxnAccount->getIValueFieldAccount(sfAuthorizedKey).getAccountID())
				{
					// Authorized to continue.
					nothing();
				}
				else if (naSigningPubKey.getAccountID() == mTxnAccountID)
				{
					// Authorized to continue.
					nothing();
				}
				else if (bHaveAuthKey)
				{
					Log(lsINFO) << "applyTransaction: Delay: Not authorized to use account.";

					terResult	= tefBAD_AUTH;
				}
				else
				{
					Log(lsINFO) << "applyTransaction: Invalid: Not authorized to use account.";

					terResult	= temBAD_AUTH_MASTER;
				}
				break;
		}
	}

	// Deduct the fee, so it's not available during the transaction.
	// Will only write the account back, if the transaction succeeds.
	if (tesSUCCESS != terResult || !saCost)
	{
		nothing();
	}
	else if (saSrcBalance < saPaid)
	{
		Log(lsINFO)
			<< boost::str(boost::format("applyTransaction: Delay: insufficient balance: balance=%s paid=%s")
				% saSrcBalance.getText()
				% saPaid.getText());

		terResult	= terINSUF_FEE_B;
	}
	else
	{
		mTxnAccount->setIFieldAmount(sfBalance, saSrcBalance - saPaid);
	}

	// Validate sequence
	if (tesSUCCESS != terResult)
	{
		nothing();
	}
	else if (saCost)
	{
		uint32 a_seq = mTxnAccount->getIFieldU32(sfSequence);

		Log(lsTRACE) << "Aseq=" << a_seq << ", Tseq=" << t_seq;

		if (t_seq != a_seq)
		{
			if (a_seq < t_seq)
			{
				Log(lsINFO) << "applyTransaction: future sequence number";

				terResult	= terPRE_SEQ;
			}
			else if (mLedger->hasTransaction(txID))
			{
				Log(lsWARNING) << "applyTransaction: duplicate sequence number";

				terResult	= tefALREADY;
			}
			else
			{
				Log(lsWARNING) << "applyTransaction: past sequence number";

				terResult	= tefPAST_SEQ;
			}
		}
		else
		{
			mTxnAccount->setIFieldU32(sfSequence, t_seq + 1);
		}
	}
	else
	{
		Log(lsINFO) << "applyTransaction: Zero cost transaction";

		if (t_seq)
		{
			Log(lsINFO) << "applyTransaction: bad sequence for pre-paid transaction";

			terResult	= tefPAST_SEQ;
		}
	}

	if (tesSUCCESS == terResult)
	{
		entryModify(mTxnAccount);

		switch (txn.getTxnType())
		{
			case ttACCOUNT_SET:
				terResult = doAccountSet(txn);
				break;

			case ttCLAIM:
				terResult = doClaim(txn);
				break;

			case ttCREDIT_SET:
				terResult = doCreditSet(txn);
				break;

			case ttINVALID:
				Log(lsINFO) << "applyTransaction: invalid type";
				terResult = temINVALID;
				break;

			//case ttINVOICE:
			//	terResult = doInvoice(txn);
			//	break;

			case ttOFFER_CREATE:
				terResult = doOfferCreate(txn);
				break;

			case ttOFFER_CANCEL:
				terResult = doOfferCancel(txn);
				break;

			case ttNICKNAME_SET:
				terResult = doNicknameSet(txn);
				break;

			case ttPASSWORD_FUND:
				terResult = doPasswordFund(txn);
				break;

			case ttPASSWORD_SET:
				terResult = doPasswordSet(txn);
				break;

			case ttPAYMENT:
				terResult = doPayment(txn);
				break;

			case ttWALLET_ADD:
				terResult = doWalletAdd(txn);
				break;

			case ttCONTRACT:
				terResult= doContractAdd(txn);
				break;
			case ttCONTRACT_REMOVE:
				terResult=doContractRemove(txn);
				break;

			default:
				terResult = temUNKNOWN;
				break;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	transResultInfo(terResult, strToken, strHuman);

	Log(lsINFO) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

	if (isTepPartial(terResult) && isSetBit(params, tapRETRY))
	{
		// Partial result and allowed to retry, reclassify as a retry.
		terResult	= terRETRY;
	}

	if (tesSUCCESS == terResult || isTepPartial(terResult))
	{
		// Transaction succeeded fully or (retries are not allowed and the transaction succeeded partially).
		txnWrite();

		Serializer s;

		txn.add(s);

		if (!mLedger->addTransaction(txID, s))
			assert(false);

		// Charge whatever fee they specified.
		mLedger->destroyCoins(saPaid.getNValue());
	}

	mTxnAccount	= SLE::pointer();
	mNodes.clear();
	musUnfundedFound.clear();

	if (!isSetBit(params, tapOPEN_LEDGER)
		&& (isTemMalformed(terResult) || isTefFailure(terResult)))
	{
		// XXX Malformed or failed transaction in closed ledger must bow out.
	}

	return terResult;
}



TER TransactionEngine::doAccountSet(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doAccountSet>";

	//
	// EmailHash
	//

	if (txn.getITFieldPresent(sfEmailHash))
	{
		uint128		uHash	= txn.getITFieldH128(sfEmailHash);

		if (!uHash)
		{
			Log(lsINFO) << "doAccountSet: unset email hash";

			mTxnAccount->makeIFieldAbsent(sfEmailHash);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set email hash";

			mTxnAccount->setIFieldH128(sfEmailHash, uHash);
		}
	}

	//
	// WalletLocator
	//

	if (txn.getITFieldPresent(sfWalletLocator))
	{
		uint256		uHash	= txn.getITFieldH256(sfWalletLocator);

		if (!uHash)
		{
			Log(lsINFO) << "doAccountSet: unset wallet locator";

			mTxnAccount->makeIFieldAbsent(sfEmailHash);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set wallet locator";

			mTxnAccount->setIFieldH256(sfWalletLocator, uHash);
		}
	}

	//
	// MessageKey
	//

	if (!txn.getITFieldPresent(sfMessageKey))
	{
		nothing();
	}
	else
	{
		Log(lsINFO) << "doAccountSet: set message key";

		mTxnAccount->setIFieldVL(sfMessageKey, txn.getITFieldVL(sfMessageKey));
	}

	//
	// Domain
	//

	if (txn.getITFieldPresent(sfDomain))
	{
		std::vector<unsigned char>	vucDomain	= txn.getITFieldVL(sfDomain);

		if (vucDomain.empty())
		{
			Log(lsINFO) << "doAccountSet: unset domain";

			mTxnAccount->makeIFieldAbsent(sfDomain);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set domain";

			mTxnAccount->setIFieldVL(sfDomain, vucDomain);
		}
	}

	//
	// TransferRate
	//

	if (txn.getITFieldPresent(sfTransferRate))
	{
		uint32		uRate	= txn.getITFieldU32(sfTransferRate);

		if (!uRate)
		{
			Log(lsINFO) << "doAccountSet: unset transfer rate";

			mTxnAccount->makeIFieldAbsent(sfTransferRate);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set transfer rate";

			mTxnAccount->setIFieldU32(sfTransferRate, uRate);
		}
	}

	//
	// PublishHash && PublishSize
	//

	bool	bPublishHash	= txn.getITFieldPresent(sfPublishHash);
	bool	bPublishSize	= txn.getITFieldPresent(sfPublishSize);

	if (bPublishHash ^ bPublishSize)
	{
		Log(lsINFO) << "doAccountSet: bad publish";

		return temBAD_PUBLISH;
	}
	else if (bPublishHash && bPublishSize)
	{
		uint256		uHash	= txn.getITFieldH256(sfPublishHash);
		uint32		uSize	= txn.getITFieldU32(sfPublishSize);

		if (!uHash)
		{
			Log(lsINFO) << "doAccountSet: unset publish";

			mTxnAccount->makeIFieldAbsent(sfPublishHash);
			mTxnAccount->makeIFieldAbsent(sfPublishSize);
		}
		else
		{
			Log(lsINFO) << "doAccountSet: set publish";

			mTxnAccount->setIFieldH256(sfPublishHash, uHash);
			mTxnAccount->setIFieldU32(sfPublishSize, uSize);
		}
	}

	Log(lsINFO) << "doAccountSet<";

	return tesSUCCESS;
}

TER TransactionEngine::doClaim(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doClaim>";

	TER	terResult	= setAuthorized(txn, true);

	Log(lsINFO) << "doClaim<";

	return terResult;
}

TER TransactionEngine::doCreditSet(const SerializedTransaction& txn)
{
	TER			terResult		= tesSUCCESS;
	Log(lsINFO) << "doCreditSet>";

	// Check if destination makes sense.
	uint160		uDstAccountID	= txn.getITFieldAccount(sfDestination);

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Destination account not specifed.";

		return temDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Can not extend credit to self.";

		return temDST_IS_SRC;
	}

	SLE::pointer		sleDst		= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		Log(lsINFO) << "doCreditSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	const bool			bFlipped		= mTxnAccountID > uDstAccountID;		// true, iff current is not lowest.
	const bool			bLimitAmount	= txn.getITFieldPresent(sfLimitAmount);
	const STAmount		saLimitAmount	= bLimitAmount ? txn.getITFieldAmount(sfLimitAmount) : STAmount();
	const bool			bQualityIn		= txn.getITFieldPresent(sfQualityIn);
	const uint32		uQualityIn		= bQualityIn ? txn.getITFieldU32(sfQualityIn) : 0;
	const bool			bQualityOut		= txn.getITFieldPresent(sfQualityOut);
	const uint32		uQualityOut		= bQualityIn ? txn.getITFieldU32(sfQualityOut) : 0;
	const uint160		uCurrencyID		= saLimitAmount.getCurrency();
	bool				bDelIndex		= false;

	SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));
	if (sleRippleState)
	{
		// A line exists in one or more directions.
#if 0
		if (!saLimitAmount)
		{
			// Zeroing line.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bLow			= uLowID == uSrcAccountID;
			bool		bHigh			= uLowID == uDstAccountID;
			bool		bBalanceZero	= !sleRippleState->getIValueFieldAmount(sfBalance);
			STAmount	saDstLimit		= sleRippleState->getIValueFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);
			bool		bDstLimitZero	= !saDstLimit;

			assert(bLow || bHigh);

			if (bBalanceZero && bDstLimitZero)
			{
				// Zero balance and eliminating last limit.

				bDelIndex	= true;
				terResult	= dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex(), false);
			}
		}
#endif

		if (!bDelIndex)
		{
			if (bLimitAmount)
				sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit: sfLowLimit , saLimitAmount);

			if (!bQualityIn)
			{
				nothing();
			}
			else if (uQualityIn)
			{
				sleRippleState->setIFieldU32(bFlipped ? sfLowQualityIn : sfHighQualityIn, uQualityIn);
			}
			else
			{
				sleRippleState->makeIFieldAbsent(bFlipped ? sfLowQualityIn : sfHighQualityIn);
			}

			if (!bQualityOut)
			{
				nothing();
			}
			else if (uQualityOut)
			{
				sleRippleState->setIFieldU32(bFlipped ? sfLowQualityOut : sfHighQualityOut, uQualityOut);
			}
			else
			{
				sleRippleState->makeIFieldAbsent(bFlipped ? sfLowQualityOut : sfHighQualityOut);
			}

			entryModify(sleRippleState);
		}

		Log(lsINFO) << "doCreditSet: Modifying ripple line: bDelIndex=" << bDelIndex;
	}
	// Line does not exist.
	else if (!saLimitAmount)
	{
		Log(lsINFO) << "doCreditSet: Redundant: Setting non-existant ripple line to 0.";

		return terNO_LINE_NO_ZERO;
	}
	else
	{
		// Create a new ripple line.
		sleRippleState	= entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		Log(lsINFO) << "doCreditSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setIFieldAmount(sfBalance, STAmount(uCurrencyID, ACCOUNT_ONE));	// Zero balance in currency.
		sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, saLimitAmount);
		sleRippleState->setIFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID, ACCOUNT_ONE));
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, mTxnAccountID);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uDstAccountID);
		if (uQualityIn)
			sleRippleState->setIFieldU32(bFlipped ? sfHighQualityIn : sfLowQualityIn, uQualityIn);
		if (uQualityOut)
			sleRippleState->setIFieldU32(bFlipped ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

		uint64			uSrcRef;							// Ignored, dirs never delete.

		terResult	= dirAdd(uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex());

		if (tesSUCCESS == terResult)
			terResult	= dirAdd(uSrcRef, Ledger::getOwnerDirIndex(uDstAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doCreditSet<";

	return terResult;
}

TER TransactionEngine::doNicknameSet(const SerializedTransaction& txn)
{
	std::cerr << "doNicknameSet>" << std::endl;

	const uint256		uNickname		= txn.getITFieldH256(sfNickname);
	const bool			bMinOffer		= txn.getITFieldPresent(sfMinimumOffer);
	const STAmount		saMinOffer		= bMinOffer ? txn.getITFieldAmount(sfAmount) : STAmount();

	SLE::pointer		sleNickname		= entryCache(ltNICKNAME, uNickname);

	if (sleNickname)
	{
		// Edit old entry.
		sleNickname->setIFieldAccount(sfAccount, mTxnAccountID);

		if (bMinOffer && saMinOffer)
		{
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
		}
		else
		{
			sleNickname->makeIFieldAbsent(sfMinimumOffer);
		}

		entryModify(sleNickname);
	}
	else
	{
		// Make a new entry.
		// XXX Need to include authorization limiting for first year.

		sleNickname	= entryCreate(ltNICKNAME, Ledger::getNicknameIndex(uNickname));

		std::cerr << "doNicknameSet: Creating nickname node: " << sleNickname->getIndex().ToString() << std::endl;

		sleNickname->setIFieldAccount(sfAccount, mTxnAccountID);

		if (bMinOffer && saMinOffer)
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
	}

	std::cerr << "doNicknameSet<" << std::endl;

	return tesSUCCESS;
}

TER TransactionEngine::doPasswordFund(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordFund>" << std::endl;

	const uint160		uDstAccountID	= txn.getITFieldAccount(sfDestination);
	SLE::pointer		sleDst			= mTxnAccountID == uDstAccountID
											? mTxnAccount
											: entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		std::cerr << "doPasswordFund: Delay transaction: Destination account does not exist." << std::endl;

		return terSET_MISSING_DST;
	}

	if (sleDst->getFlags() & lsfPasswordSpent)
	{
		sleDst->clearFlag(lsfPasswordSpent);

		std::cerr << "doPasswordFund: Clearing spent." << sleDst->getFlags() << std::endl;

		if (mTxnAccountID != uDstAccountID) {
			std::cerr << "doPasswordFund: Destination modified." << std::endl;

			entryModify(sleDst);
		}
	}

	std::cerr << "doPasswordFund<" << std::endl;

	return tesSUCCESS;
}

TER TransactionEngine::doPasswordSet(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordSet>" << std::endl;

	if (mTxnAccount->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doPasswordSet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	mTxnAccount->setFlag(lsfPasswordSpent);

	TER	terResult	= setAuthorized(txn, false);

	std::cerr << "doPasswordSet<" << std::endl;

	return terResult;
}

// If needed, advance to next funded offer.
// - Automatically advances to first offer.
// - Set bEntryAdvance to advance to next entry.
// <-- uOfferIndex : 0=end of list.
TER TransactionEngine::calcNodeAdvance(
	const unsigned int			uIndex,				// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality,
	const bool					bReverse)
{
	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];

	const uint160&	uPrvCurrencyID	= pnPrv.uCurrencyID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const uint160&	uCurCurrencyID	= pnCur.uCurrencyID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;

	uint256&		uDirectTip		= pnCur.uDirectTip;
	uint256			uDirectEnd		= pnCur.uDirectEnd;
	bool&			bDirectAdvance	= pnCur.bDirectAdvance;
	SLE::pointer&	sleDirectDir	= pnCur.sleDirectDir;
	STAmount&		saOfrRate		= pnCur.saOfrRate;

	bool&			bEntryAdvance	= pnCur.bEntryAdvance;
	unsigned int&	uEntry			= pnCur.uEntry;
	uint256&		uOfferIndex		= pnCur.uOfferIndex;
	SLE::pointer&	sleOffer		= pnCur.sleOffer;
	uint160&		uOfrOwnerID		= pnCur.uOfrOwnerID;
	STAmount&		saOfferFunds	= pnCur.saOfferFunds;
	STAmount&		saTakerPays		= pnCur.saTakerPays;
	STAmount&		saTakerGets		= pnCur.saTakerGets;
	bool&			bFundsDirty		= pnCur.bFundsDirty;

	TER				terResult		= tesSUCCESS;

	do
	{
		bool	bDirectDirDirty	= false;

		if (!uDirectEnd)
		{
			// Need to initialize current node.

			uDirectTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
			uDirectEnd		= Ledger::getQualityNext(uDirectTip);
			sleDirectDir	= entryCache(ltDIR_NODE, uDirectTip);
			bDirectAdvance	= !sleDirectDir;
			bDirectDirDirty	= true;

			Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: Initialize node: uDirectTip=%s uDirectEnd=%s bDirectAdvance=%d") % uDirectTip % uDirectEnd % bDirectAdvance);
		}

		if (bDirectAdvance)
		{
			// Get next quality.
			uDirectTip		= mLedger->getNextLedgerIndex(uDirectTip, uDirectEnd);
			bDirectDirDirty	= true;
			bDirectAdvance	= false;

			if (!!uDirectTip)
			{
				// Have another quality directory.
				Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: Quality advance: uDirectTip=%s") % uDirectTip);

				sleDirectDir	= entryCache(ltDIR_NODE, uDirectTip);
			}
			else if (bReverse)
			{
				Log(lsINFO) << "calcNodeAdvance: No more offers.";

				uOfferIndex	= 0;
				break;
			}
			else
			{
				// No more offers. Should be done rather than fall off end of book.
				Log(lsINFO) << "calcNodeAdvance: Unreachable: Fell off end of order book.";
				assert(false);

				terResult	= tefEXCEPTION;
			}
		}

		if (bDirectDirDirty)
		{
			saOfrRate		= STAmount::setRate(Ledger::getQuality(uDirectTip));	// For correct ratio
			uEntry			= 0;
			bEntryAdvance	= true;

			Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: directory dirty: saOfrRate=%s") % saOfrRate);
		}

		if (!bEntryAdvance)
		{
			if (bFundsDirty)
			{
				saTakerPays		= sleOffer->getIValueFieldAmount(sfTakerPays);
				saTakerGets		= sleOffer->getIValueFieldAmount(sfTakerGets);

				saOfferFunds	= accountFunds(uOfrOwnerID, saTakerGets);	// Funds left.
				bFundsDirty		= false;

				Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: directory dirty: saOfrRate=%s") % saOfrRate);
			}
			else
			{
				Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: as is"));
				nothing();
			}
		}
		else if (!dirNext(uDirectTip, sleDirectDir, uEntry, uOfferIndex))
		{
			// Failed to find an entry in directory.

			uOfferIndex	= 0;

			// Do another cur directory iff bMultiQuality
			if (bMultiQuality)
			{
				Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: next quality"));
				bDirectAdvance	= true;
			}
			else if (!bReverse)
			{
				Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: unreachable: ran out of offers"));
				assert(false);		// Can't run out of offers in forward direction.
				terResult	= tefEXCEPTION;
			}
		}
		else
		{
			// Got a new offer.
			sleOffer	= entryCache(ltOFFER, uOfferIndex);
			uOfrOwnerID = sleOffer->getIValueFieldAccount(sfAccount).getAccountID();

			const aciSource			asLine				= boost::make_tuple(uOfrOwnerID, uCurCurrencyID, uCurIssuerID);

			Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: uOfrOwnerID=%s") % NewcoinAddress::createHumanAccountID(uOfrOwnerID));

			if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
			{
				// Offer is expired.
				Log(lsINFO) << "calcNodeAdvance: expired offer";

				assert(musUnfundedFound.find(uOfferIndex) != musUnfundedFound.end());	// Verify reverse found it too.
				bEntryAdvance	= true;
				continue;
			}

			// Allowed to access source from this node?
			// XXX This can get called multiple times for same source in a row, caching result would be nice.
			curIssuerNodeConstIterator	itForward		= pspCur->umForward.find(asLine);
			const bool					bFoundForward	= itForward != pspCur->umForward.end();

			if (bFoundForward && itForward->second != uIndex)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				Log(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (forward)";

				bEntryAdvance	= true;
				continue;
			}

			curIssuerNodeConstIterator	itPast			= mumSource.find(asLine);
			bool						bFoundPast		= itPast != mumSource.end();

			if (bFoundPast && itPast->second != uIndex)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				Log(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (past)";

				bEntryAdvance	= true;
				continue;
			}

			curIssuerNodeConstIterator	itReverse		= pspCur->umReverse.find(asLine);
			bool						bFoundReverse	= itReverse != pspCur->umReverse.end();

			if (bFoundReverse && itReverse->second != uIndex)
			{
				// Temporarily unfunded. Another node uses this source, ignore in this offer.
				Log(lsINFO) << "calcNodeAdvance: temporarily unfunded offer (reverse)";

				bEntryAdvance	= true;
				continue;
			}

			saTakerPays		= sleOffer->getIValueFieldAmount(sfTakerPays);
			saTakerGets		= sleOffer->getIValueFieldAmount(sfTakerGets);

			saOfferFunds	= accountFunds(uOfrOwnerID, saTakerGets);	// Funds left.

			if (!saOfferFunds.isPositive())
			{
				// Offer is unfunded.
				Log(lsINFO) << "calcNodeAdvance: unfunded offer";

				if (bReverse && !bFoundReverse && !bFoundPast)
				{
					// Never mentioned before: found unfunded.
					musUnfundedFound.insert(uOfferIndex);				// Mark offer for always deletion.
				}

				// YYY Could verify offer is correct place for unfundeds.
				bEntryAdvance	= true;
				continue;
			}

			if (bReverse			// Need to remember reverse mention.
				&& !bFoundPast		// Not mentioned in previous passes.
				&& !bFoundReverse)	// Not mentioned for pass.
			{
				// Consider source mentioned by current path state.
				Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: remember=%s/%s/%s")
					% NewcoinAddress::createHumanAccountID(uOfrOwnerID)
					% STAmount::createHumanCurrency(uCurCurrencyID)
					% NewcoinAddress::createHumanAccountID(uCurIssuerID));

				pspCur->umReverse.insert(std::make_pair(asLine, uIndex));
			}

			bFundsDirty		= false;
			bEntryAdvance	= false;
		}
	}
	while (tesSUCCESS == terResult && (bEntryAdvance || bDirectAdvance));

	if (tesSUCCESS == terResult)
	{
		Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: uOfferIndex=%s") % uOfferIndex);
	}
	else
	{
		Log(lsINFO) << boost::str(boost::format("calcNodeAdvance: terResult=%s") % transToken(terResult));
	}

	return terResult;
}

// Between offer nodes, the fee charged may vary.  Therefore, process one inbound offer at a time.
// Propagate the inbound offer's requirements to the previous node.  The previous node adjusts the amount output and the
// amount spent on fees.
// Continue process till request is satisified while we the rate does not increase past the initial rate.
TER TransactionEngine::calcNodeDeliverRev(
	const unsigned int			uIndex,			// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality,
	const uint160&				uOutAccountID,	// --> Output owner's account.
	const STAmount&				saOutReq,		// --> Funds wanted.
	STAmount&					saOutAct)		// <-- Funds delivered.
{
	TER	terResult	= tesSUCCESS;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];

	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uPrvAccountID	= pnPrv.uAccountID;
	const STAmount&	saTransferRate	= pnCur.saTransferRate;

	STAmount&		saPrvDlvReq		= pnPrv.saRevDeliver;	// To be adjusted.

	saOutAct	= 0;

	while (saOutAct != saOutReq)							// Did not deliver limit.
	{
		bool&			bEntryAdvance	= pnCur.bEntryAdvance;
		STAmount&		saOfrRate		= pnCur.saOfrRate;
		uint256&		uOfferIndex		= pnCur.uOfferIndex;
		SLE::pointer&	sleOffer		= pnCur.sleOffer;
		const uint160&	uOfrOwnerID		= pnCur.uOfrOwnerID;
		bool&			bFundsDirty		= pnCur.bFundsDirty;
		STAmount&		saOfferFunds	= pnCur.saOfferFunds;
		STAmount&		saTakerPays		= pnCur.saTakerPays;
		STAmount&		saTakerGets		= pnCur.saTakerGets;
		STAmount&		saRateMax		= pnCur.saRateMax;

		terResult	= calcNodeAdvance(uIndex, pspCur, bMultiQuality, true);		// If needed, advance to next funded offer.

		if (tesSUCCESS != terResult || !uOfferIndex)
		{
			// Error or out of offers.
			break;
		}

		const STAmount	saOutFeeRate	= uOfrOwnerID == uCurIssuerID || uOutAccountID == uCurIssuerID // Issuer receiving or sending.
										? saOne				// No fee.
										: saTransferRate;	// Transfer rate of issuer.
		Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: uOfrOwnerID=%s uOutAccountID=%s uCurIssuerID=%s saTransferRate=%s saOutFeeRate=%s")
			% NewcoinAddress::createHumanAccountID(uOfrOwnerID)
			% NewcoinAddress::createHumanAccountID(uOutAccountID)
			% NewcoinAddress::createHumanAccountID(uCurIssuerID)
			% saTransferRate.getFullText()
			% saOutFeeRate.getFullText());

		if (!saRateMax)
		{
			// Set initial rate.
			saRateMax	= saOutFeeRate;

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Set initial rate: saRateMax=%s saOutFeeRate=%s")
				% saRateMax
				% saOutFeeRate);
		}
		else if (saRateMax < saOutFeeRate)
		{
			// Offer exceeds initial rate.
			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Offer exceeds initial rate: saRateMax=%s saOutFeeRate=%s")
				% saRateMax
				% saOutFeeRate);

			nothing();
			break;
		}
		else if (saOutFeeRate < saRateMax)
		{
			// Reducing rate.

			saRateMax	= saOutFeeRate;

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Reducing rate: saRateMax=%s")
				% saRateMax);
		}

		STAmount	saOutPass		= std::min(std::min(saOfferFunds, saTakerGets), saOutReq-saOutAct);	// Offer maximum out - assuming no out fees.
		STAmount	saOutPlusFees	= STAmount::multiply(saOutPass, saOutFeeRate);						// Offer out with fees.

		Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: saOutReq=%s saOutAct=%s saTakerGets=%s saOutPass=%s saOutPlusFees=%s saOfferFunds=%s")
			% saOutReq
			% saOutAct
			% saTakerGets
			% saOutPass
			% saOutPlusFees
			% saOfferFunds);

		if (saOutPlusFees > saOfferFunds)
		{
			// Offer owner can not cover all fees, compute saOutPass based on saOfferFunds.

			saOutPlusFees	= saOfferFunds;
			saOutPass		= STAmount::divide(saOutPlusFees, saOutFeeRate);

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: Total exceeds fees: saOutPass=%s saOutPlusFees=%s saOfferFunds=%s")
				% saOutPass
				% saOutPlusFees
				% saOfferFunds);
		}

		// Compute portion of input needed to cover output.

		STAmount	saInPassReq	= STAmount::multiply(saOutPass, saOfrRate, saTakerPays);
		STAmount	saInPassAct;

		Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: saInPassReq=%s saOfrRate=%s saOutPass=%s saOutPlusFees=%s")
			% saInPassReq
			% saOfrRate
			% saOutPass
			% saOutPlusFees);

		// Find out input amount actually available at current rate.
		if (!!uPrvAccountID)
		{
			// account --> OFFER --> ?
			// Previous is the issuer and receiver is an offer, so no fee or quality.
			// Previous is the issuer and has unlimited funds.
			// Offer owner is obtaining IOUs via an offer, so credit line limits are ignored.
			// As limits are ignored, don't need to adjust previous account's balance.

			saInPassAct	= saInPassReq;

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: account --> OFFER --> ? : saInPassAct=%s")
				% saPrvDlvReq);
		}
		else
		{
			// offer --> OFFER --> ?

			terResult	= calcNodeDeliverRev(
				uIndex-1,
				pspCur,
				bMultiQuality,
				uOfrOwnerID,
				saInPassReq,
				saInPassAct);

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: offer --> OFFER --> ? : saInPassAct=%s")
				% saInPassAct);
		}

		if (tesSUCCESS != terResult)
			break;

		if (saInPassAct != saInPassReq)
		{
			// Adjust output to conform to limited input.
			saOutPass		= STAmount::divide(saInPassAct, saOfrRate, saTakerGets);
			saOutPlusFees	= STAmount::multiply(saOutPass, saOutFeeRate);

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: adjusted: saOutPass=%s saOutPlusFees=%s")
				% saOutPass
				% saOutPlusFees);
		}

		// Funds were spent.
		bFundsDirty		= true;

		// Deduct output, don't actually need to send.
		accountSend(uOfrOwnerID, uCurIssuerID, saOutPass);

		// Adjust offer
		sleOffer->setIFieldAmount(sfTakerGets, saTakerGets - saOutPass);
		sleOffer->setIFieldAmount(sfTakerPays, saTakerPays - saInPassAct);

		entryModify(sleOffer);

		if (saOutPass == saTakerGets)
		{
			// Offer became unfunded.
			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverRev: offer became unfunded."));

			bEntryAdvance	= true;
		}

		saOutAct	+= saOutPass;
		saPrvDlvReq	+= saInPassAct;
	}

	if (!saOutAct)
		terResult	= tepPATH_DRY;

	return terResult;
}

// Deliver maximum amount of funds from previous node.
// Goal: Make progress consuming the offer.
TER TransactionEngine::calcNodeDeliverFwd(
	const unsigned int			uIndex,			// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality,
	const uint160&				uInAccountID,	// --> Input owner's account.
	const STAmount&				saInFunds,		// --> Funds available for delivery and fees.
	const STAmount&				saInReq,		// --> Limit to deliver.
	STAmount&					saInAct,		// <-- Amount delivered.
	STAmount&					saInFees)		// <-- Fees charged.
{
	TER	terResult	= tesSUCCESS;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uIndex+1];

	const uint160&	uNxtAccountID	= pnNxt.uAccountID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const STAmount&	saTransferRate	= pnPrv.saTransferRate;

	saInAct		= 0;
	saInFees	= 0;

	while (tesSUCCESS == terResult
		&& saInAct != saInReq					// Did not deliver limit.
		&& saInAct + saInFees != saInFunds)		// Did not deliver all funds.
	{
		terResult	= calcNodeAdvance(uIndex, pspCur, bMultiQuality, false);				// If needed, advance to next funded offer.

		if (tesSUCCESS == terResult)
		{
			bool&			bEntryAdvance	= pnCur.bEntryAdvance;
			STAmount&		saOfrRate		= pnCur.saOfrRate;
			uint256&		uOfferIndex		= pnCur.uOfferIndex;
			SLE::pointer&	sleOffer		= pnCur.sleOffer;
			const uint160&	uOfrOwnerID		= pnCur.uOfrOwnerID;
			bool&			bFundsDirty		= pnCur.bFundsDirty;
			STAmount&		saOfferFunds	= pnCur.saOfferFunds;
			STAmount&		saTakerPays		= pnCur.saTakerPays;
			STAmount&		saTakerGets		= pnCur.saTakerGets;


			const STAmount	saInFeeRate	= uInAccountID == uPrvIssuerID || uOfrOwnerID == uPrvIssuerID	// Issuer receiving or sending.
											? saOne				// No fee.
											: saTransferRate;	// Transfer rate of issuer.

			//
			// First calculate assuming no output fees.
			// XXX Make sure derived in does not exceed actual saTakerPays due to rounding.

			STAmount	saOutFunded		= std::max(saOfferFunds, saTakerGets);						// Offer maximum out - There are no out fees.
			STAmount	saInFunded		= STAmount::multiply(saOutFunded, saOfrRate, saInReq);		// Offer maximum in - Limited by by payout.
			STAmount	saInTotal		= STAmount::multiply(saInFunded, saTransferRate);			// Offer maximum in with fees.
			STAmount	saInSum			= std::min(saInTotal, saInFunds-saInAct-saInFees);			// In limited by saInFunds.
			STAmount	saInPassAct		= STAmount::divide(saInSum, saInFeeRate);					// In without fees.
			STAmount	saOutPassMax	= STAmount::divide(saInPassAct, saOfrRate, saOutFunded);	// Out.

			STAmount	saInPassFees;
			STAmount	saOutPassAct;

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: saOutFunded=%s saInFunded=%s saInTotal=%s saInSum=%s saInPassAct=%s saOutPassMax=%s")
				% saOutFunded
				% saInFunded
				% saInTotal
				% saInSum
				% saInPassAct
				% saOutPassMax);

			if (!!uNxtAccountID)
			{
				// ? --> OFFER --> account
				// Input fees: vary based upon the consumed offer's owner.
				// Output fees: none as the destination account is the issuer.

				// XXX This doesn't claim input.
				// XXX Assumes input is in limbo.  XXX Check.

				// Debit offer owner.
				accountSend(uOfrOwnerID, uCurIssuerID, saOutPassMax);

				saOutPassAct	= saOutPassMax;

				Log(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: ? --> OFFER --> account: saOutPassAct=%s")
					% saOutPassAct);
			}
			else
			{
				// ? --> OFFER --> offer
				STAmount	saOutPassFees;

				terResult	= TransactionEngine::calcNodeDeliverFwd(
					uIndex+1,
					pspCur,
					bMultiQuality,
					uOfrOwnerID,
					saOutPassMax,
					saOutPassMax,
					saOutPassAct,		// <-- Amount delivered.
					saOutPassFees);		// <-- Fees charged.

				if (tesSUCCESS != terResult)
					break;

				// Offer maximum in limited by next payout.
				saInPassAct			= STAmount::multiply(saOutPassAct, saOfrRate);
				saInPassFees		= STAmount::multiply(saInFunded, saInFeeRate)-saInPassAct;
			}

			Log(lsINFO) << boost::str(boost::format("calcNodeDeliverFwd: saTakerGets=%s saTakerPays=%s saInPassAct=%s saOutPassAct=%s")
				% saTakerGets.getFullText()
				% saTakerPays.getFullText()
				% saInPassAct.getFullText()
				% saOutPassAct.getFullText());

			// Funds were spent.
			bFundsDirty		= true;

			// Credit issuer transfer fees.
			accountSend(uInAccountID, uOfrOwnerID, saInPassFees);

			// Credit offer owner from offer.
			accountSend(uInAccountID, uOfrOwnerID, saInPassAct);

			// Adjust offer
			sleOffer->setIFieldAmount(sfTakerGets, saTakerGets - saOutPassAct);
			sleOffer->setIFieldAmount(sfTakerPays, saTakerPays - saInPassAct);

			entryModify(sleOffer);

			if (saOutPassAct == saTakerGets)
			{
				// Offer became unfunded.
				pspCur->vUnfundedBecame.push_back(uOfferIndex);
				bEntryAdvance	= true;
			}

			saInAct		+= saInPassAct;
			saInFees	+= saInPassFees;
		}
	}

	return terResult;
}

// Called to drive from the last offer node in a chain.
TER TransactionEngine::calcNodeOfferRev(
	const unsigned int			uIndex,				// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality)
{
	TER				terResult;

	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uIndex+1];

	if (!!pnNxt.uAccountID)
	{
		// Next is an account node, resolve current offer node's deliver.
		STAmount		saDeliverAct;

		terResult	= calcNodeDeliverRev(
							uIndex,
							pspCur,
							bMultiQuality,

							pnNxt.uAccountID,
							pnCur.saRevDeliver,
							saDeliverAct);
	}
	else
	{
		// Next is an offer. Deliver has already been resolved.
		terResult	= tesSUCCESS;
	}

	return terResult;
}

// Called to drive the from the first offer node in a chain.
// - Offer input is limbo.
// - Current offers consumed.
//   - Current offer owners debited.
//   - Transfer fees credited to issuer.
//   - Payout to issuer or limbo.
// - Deliver is set without transfer fees.
TER TransactionEngine::calcNodeOfferFwd(
	const unsigned int			uIndex,				// 0 < uIndex < uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality
	)
{
	TER				terResult;
	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];

	if (!!pnPrv.uAccountID)
	{
		// Previous is an account node, resolve its deliver.
		STAmount		saInAct;
		STAmount		saInFees;

		terResult	= calcNodeDeliverFwd(
							uIndex,
							pspCur,
							bMultiQuality,
							pnPrv.uAccountID,
							pnPrv.saFwdDeliver,
							pnPrv.saFwdDeliver,
							saInAct,
							saInFees);

		assert(tesSUCCESS != terResult || pnPrv.saFwdDeliver == saInAct+saInFees);
	}
	else
	{
		// Previous is an offer. Deliver has already been resolved.
		terResult	= tesSUCCESS;
	}

	return terResult;

}

// Cur is the driver and will be filled exactly.
// uQualityIn -> uQualityOut
//   saPrvReq -> saCurReq
//   sqPrvAct -> saCurAct
// This is a minimizing routine: moving in reverse it propagates the send limit to the sender, moving forward it propagates the
// actual send toward the receiver.
// This routine works backwards as it calculates previous wants based on previous credit limits and current wants.
// This routine works forwards as it calculates current deliver based on previous delivery limits and current wants.
// XXX Deal with uQualityIn or uQualityOut = 0
void TransactionEngine::calcNodeRipple(
	const uint32 uQualityIn,
	const uint32 uQualityOut,
	const STAmount& saPrvReq,	// --> in limit including fees, <0 = unlimited
	const STAmount& saCurReq,	// --> out limit (driver)
	STAmount& saPrvAct,			// <-> in limit including achieved
	STAmount& saCurAct,			// <-> out limit achieved.
	uint64& uRateMax)
{
	Log(lsINFO) << boost::str(boost::format("calcNodeRipple> uQualityIn=%d uQualityOut=%d saPrvReq=%s saCurReq=%s saPrvAct=%s saCurAct=%s")
		% uQualityIn
		% uQualityOut
		% saPrvReq.getFullText()
		% saCurReq.getFullText()
		% saPrvAct.getFullText()
		% saCurAct.getFullText());

	assert(saPrvReq.getCurrency() == saCurReq.getCurrency());

	const bool		bPrvUnlimited	= saPrvReq.isNegative();
	const STAmount	saPrv			= bPrvUnlimited ? STAmount(saPrvReq) : saPrvReq-saPrvAct;
	const STAmount	saCur			= saCurReq-saCurAct;

#if 0
	Log(lsINFO) << boost::str(boost::format("calcNodeRipple: bPrvUnlimited=%d saPrv=%s saCur=%s")
		% bPrvUnlimited
		% saPrv.getFullText()
		% saCur.getFullText());
#endif

	if (uQualityIn >= uQualityOut)
	{
		// No fee.
		Log(lsINFO) << boost::str(boost::format("calcNodeRipple: No fees"));

		if (!uRateMax || STAmount::uRateOne <= uRateMax)
		{
			STAmount	saTransfer	= bPrvUnlimited ? saCur : std::min(saPrv, saCur);

			saPrvAct	+= saTransfer;
			saCurAct	+= saTransfer;

			if (!uRateMax)
				uRateMax	= STAmount::uRateOne;
		}
	}
	else
	{
		// Fee.
		Log(lsINFO) << boost::str(boost::format("calcNodeRipple: Fee"));

		uint64	uRate	= STAmount::getRate(STAmount(uQualityIn), STAmount(uQualityOut));

		if (!uRateMax || uRate <= uRateMax)
		{
			const uint160	uCurrencyID		= saCur.getCurrency();
			const uint160	uCurIssuerID	= saCur.getIssuer();
			const uint160	uPrvIssuerID	= saPrv.getIssuer();

			STAmount	saCurIn		= STAmount::divide(STAmount::multiply(saCur, uQualityOut, uCurrencyID, uCurIssuerID), uQualityIn, uCurrencyID, uCurIssuerID);

	Log(lsINFO) << boost::str(boost::format("calcNodeRipple: bPrvUnlimited=%d saPrv=%s saCurIn=%s") % bPrvUnlimited % saPrv.getFullText() % saCurIn.getFullText());
			if (bPrvUnlimited || saCurIn <= saPrv)
			{
				// All of cur. Some amount of prv.
				saCurAct	+= saCur;
				saPrvAct	+= saCurIn;
	Log(lsINFO) << boost::str(boost::format("calcNodeRipple:3c: saCurReq=%s saPrvAct=%s") % saCurReq.getFullText() % saPrvAct.getFullText());
			}
			else
			{
				// A part of cur. All of prv. (cur as driver)
				STAmount	saCurOut	= STAmount::divide(STAmount::multiply(saPrv, uQualityIn, uCurrencyID, uCurIssuerID), uQualityOut, uCurrencyID, uCurIssuerID);
	Log(lsINFO) << boost::str(boost::format("calcNodeRipple:4: saCurReq=%s") % saCurReq.getFullText());

				saCurAct	+= saCurOut;
				saPrvAct	= saPrvReq;

				if (!uRateMax)
					uRateMax	= uRate;
			}
		}
	}

	Log(lsINFO) << boost::str(boost::format("calcNodeRipple< uQualityIn=%d uQualityOut=%d saPrvReq=%s saCurReq=%s saPrvAct=%s saCurAct=%s")
		% uQualityIn
		% uQualityOut
		% saPrvReq.getFullText()
		% saCurReq.getFullText()
		% saPrvAct.getFullText()
		% saCurAct.getFullText());
}

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver from saCur...
// <-- tesSUCCESS or tepPATH_DRY
TER TransactionEngine::calcNodeAccountRev(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality)
{
	TER					terResult		= tesSUCCESS;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	uint64				uRateMax		= 0;

	PaymentNode&		pnPrv			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	PaymentNode&		pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&		pnNxt			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	// Current is allowed to redeem to next.
	const bool			bPrvAccount		= !uIndex || isSetBit(pnPrv.uFlags, STPathElement::typeAccount);
	const bool			bNxtAccount		= uIndex == uLast || isSetBit(pnNxt.uFlags, STPathElement::typeAccount);

	const uint160&		uCurAccountID	= pnCur.uAccountID;
	const uint160&		uPrvAccountID	= bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
	const uint160&		uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&		uCurrencyID		= pnCur.uCurrencyID;

	const uint32		uQualityIn		= uIndex ? rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : QUALITY_ONE;
	const uint32		uQualityOut		= uIndex != uLast ? rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : QUALITY_ONE;

	// For bPrvAccount
	const STAmount		saPrvOwed		= bPrvAccount && uIndex								// Previous account is owed.
											? rippleOwed(uCurAccountID, uPrvAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	const STAmount		saPrvLimit		= bPrvAccount && uIndex								// Previous account may owe.
											? rippleLimit(uCurAccountID, uPrvAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	const STAmount		saNxtOwed		= bNxtAccount && uIndex != uLast					// Next account is owed.
											? rippleOwed(uCurAccountID, uNxtAccountID, uCurrencyID)
											: STAmount(uCurrencyID, uCurAccountID);

	Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev> uIndex=%d/%d uPrvAccountID=%s uCurAccountID=%s uNxtAccountID=%s uCurrencyID=%s uQualityIn=%d uQualityOut=%d saPrvOwed=%s saPrvLimit=%s")
		% uIndex
		% uLast
		% NewcoinAddress::createHumanAccountID(uPrvAccountID)
		% NewcoinAddress::createHumanAccountID(uCurAccountID)
		% NewcoinAddress::createHumanAccountID(uNxtAccountID)
		% STAmount::createHumanCurrency(uCurrencyID)
		% uQualityIn
		% uQualityOut
		% saPrvOwed.getFullText()
		% saPrvLimit.getFullText());

	// Previous can redeem the owed IOUs it holds.
	const STAmount	saPrvRedeemReq	= saPrvOwed.isPositive() ? saPrvOwed : STAmount(uCurrencyID, 0);
	STAmount&		saPrvRedeemAct	= pnPrv.saRevRedeem;

	// Previous can issue up to limit minus whatever portion of limit already used (not including redeemable amount).
	const STAmount	saPrvIssueReq	= saPrvOwed.isNegative() ? saPrvLimit+saPrvOwed : saPrvLimit;
	STAmount&		saPrvIssueAct	= pnPrv.saRevIssue;

	// For !bPrvAccount
	const STAmount	saPrvDeliverReq	= STAmount::saFromSigned(uCurrencyID, uCurAccountID, -1);	// Unlimited.
	STAmount&		saPrvDeliverAct	= pnPrv.saRevDeliver;

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount		saCurRedeemAct(saCurRedeemReq.getCurrency(), saCurRedeemReq.getIssuer());

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount		saCurIssueAct(saCurIssueReq.getCurrency(), saCurIssueReq.getIssuer());					// Track progress.

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount		saCurDeliverAct(saCurDeliverReq.getCurrency(), saCurDeliverReq.getIssuer());

	Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: saPrvRedeemReq=%s saPrvIssueReq=%s saCurRedeemReq=%s saNxtOwed=%s")
		% saPrvRedeemReq.getFullText()
		% saPrvIssueReq.getFullText()
		% saCurRedeemReq.getFullText()
		% saNxtOwed.getFullText());

	Log(lsINFO) << pspCur->getJson();

	assert(!saCurRedeemReq || (-saNxtOwed) >= saCurRedeemReq);	// Current redeem req can't be more than IOUs on hand.
	assert(!saCurIssueReq || !saNxtOwed.isPositive() || saNxtOwed == saCurRedeemReq);	// If issue req, then redeem req must consume all owed.

	if (bPrvAccount && bNxtAccount)
	{
		if (!uIndex)
		{
			// ^ --> ACCOUNT -->  account|offer
			// Nothing to do, there is no previous to adjust.
			nothing();
		}
		else if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $
			// Overall deliverable.
			const STAmount&	saCurWantedReq	= bPrvAccount
												? std::min(pspCur->saOutReq, saPrvLimit+saPrvOwed)	// If previous is an account, limit.
												: pspCur->saOutReq;									// Previous is an offer, no limit: redeem own IOUs.
			STAmount		saCurWantedAct(saCurWantedReq.getCurrency(), saCurWantedReq.getIssuer());

			Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: account --> ACCOUNT --> $ : saCurWantedReq=%s")
				% saCurWantedReq.getFullText());

			// Calculate redeem
			if (saPrvRedeemReq)							// Previous has IOUs to redeem.
			{
				// Redeem at 1:1
				Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Redeem at 1:1"));

				saCurWantedAct		= std::min(saPrvRedeemReq, saCurWantedReq);
				saPrvRedeemAct		= saCurWantedAct;

				uRateMax			= STAmount::uRateOne;
			}

			// Calculate issuing.
			if (saCurWantedReq != saCurWantedAct		// Need more.
				&& saPrvIssueReq)						// Will accept IOUs from prevous.
			{
				// Rate: quality in : 1.0
				Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0"));

				// If we previously redeemed and this has a poorer rate, this won't be included the current increment.
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq, saPrvIssueAct, saCurWantedAct, uRateMax);
			}

			if (!saCurWantedAct)
			{
				// Must have processed something.
				terResult	= tepPATH_DRY;
			}
		}
		else
		{
			// ^|account --> ACCOUNT --> account

			// redeem (part 1) -> redeem
			if (saCurRedeemReq							// Next wants IOUs redeemed.
				&& saPrvRedeemReq)						// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate : 1.0 : quality out"));

				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct, uRateMax);
			}

			// issue (part 1) -> redeem
			if (saCurRedeemReq != saCurRedeemAct		// Next wants more IOUs redeemed.
				&& saPrvRedeemAct == saPrvRedeemReq)	// Previous has no IOUs to redeem remaining.
			{
				// Rate: quality in : quality out
				Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : quality out"));

				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct, uRateMax);
			}

			// redeem (part 2) -> issue.
			if (saCurIssueReq							// Next wants IOUs issued.
				&& saCurRedeemAct == saCurRedeemReq		// Can only issue if completed redeeming.
				&& saPrvRedeemAct != saPrvRedeemReq)	// Did not complete redeeming previous IOUs.
			{
				// Rate : 1.0 : transfer_rate
				Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate : 1.0 : transfer_rate"));

				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct, uRateMax);
			}

			// issue (part 2) -> issue
			if (saCurIssueReq != saCurIssueAct			// Need wants more IOUs issued.
				&& saCurRedeemAct == saCurRedeemReq		// Can only issue if completed redeeming.
				&& saPrvRedeemReq == saPrvRedeemAct)	// Previously redeemed all owed IOUs.
			{
				// Rate: quality in : 1.0
				Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0"));

				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct, uRateMax);
			}

			if (!saCurRedeemAct && !saCurIssueAct)
			{
				// Must want something.
				terResult	= tepPATH_DRY;
			}

			Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: ^|account --> ACCOUNT --> account : saCurRedeemReq=%s saCurIssueReq=%s saPrvOwed=%s saCurRedeemAct=%s saCurIssueAct=%s")
				% saCurRedeemReq.getFullText()
				% saCurIssueReq.getFullText()
				% saPrvOwed.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueAct.getFullText());
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		// Note: deliver is always issue as ACCOUNT is the issuer for the offer input.
		Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: account --> ACCOUNT --> offer"));

		// redeem -> deliver/issue.
		if (saPrvOwed.isPositive()					// Previous has IOUs to redeem.
			&& saCurDeliverReq)						// Need some issued.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct, uRateMax);
		}

		// issue -> deliver/issue
		if (saPrvRedeemReq == saPrvRedeemAct		// Previously redeemed all owed.
			&& saCurDeliverReq != saCurDeliverAct)	// Still need some issued.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct, uRateMax);
		}

		if (!saCurDeliverAct)
		{
			// Must want something.
			terResult	= tepPATH_DRY;
		}

		Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: saCurDeliverReq=%s saCurDeliverAct=%s saPrvOwed=%s")
			% saCurDeliverReq.getFullText()
			% saCurDeliverAct.getFullText()
			% saPrvOwed.getFullText());
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $
			const STAmount&	saCurWantedReq	= bPrvAccount
												? std::min(pspCur->saOutReq, saPrvLimit+saPrvOwed)	// If previous is an account, limit.
												: pspCur->saOutReq;									// Previous is an offer, no limit: redeem own IOUs.
			STAmount		saCurWantedAct(saCurWantedReq.getCurrency(), saCurWantedReq.getIssuer());

			Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> $ : saCurWantedReq=%s")
				% saCurWantedReq.getFullText());

			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvDeliverReq, saCurWantedReq, saPrvDeliverAct, saCurWantedAct, uRateMax);

			if (!saCurWantedAct)
			{
				// Must have processed something.
				terResult	= tepPATH_DRY;
			}
		}
		else
		{
			// offer --> ACCOUNT --> account
			// Note: offer is always delivering(redeeming) as account is issuer.
			Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> account"));

			// deliver -> redeem
			if (saCurRedeemReq)							// Next wants us to redeem.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct, uRateMax);
			}

			// deliver -> issue.
			if (saCurRedeemReq == saCurRedeemAct		// Can only issue if previously redeemed all.
				&& saCurIssueReq)						// Need some issued.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct, saCurIssueAct, uRateMax);
			}

			Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: saCurRedeemReq=%s saCurIssueAct=%s saCurIssueReq=%s saPrvDeliverAct=%s")
				% saCurRedeemReq.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueReq.getFullText()
				% saPrvDeliverAct.getFullText());

			if (!saPrvDeliverAct)
			{
				// Must want something.
				terResult	= tepPATH_DRY;
			}
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		Log(lsINFO) << boost::str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> offer"));

		// Rate : 1.0 : transfer_rate
		calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct, uRateMax);

		if (!saCurDeliverAct)
		{
			// Must want something.
			terResult	= tepPATH_DRY;
		}
	}

	return terResult;
}

// Perfrom balance adjustments between previous and current node.
// - The previous node: specifies what to push through to current.
// - All of previous output is consumed.
// Then, compute output for next node.
// - Current node: specify what to push through to next.
// - Output to next node is computed as input minus quality or transfer fee.
TER TransactionEngine::calcNodeAccountFwd(
	const unsigned int			uIndex,				// 0 <= uIndex <= uLast
	const PathState::pointer&	pspCur,
	const bool					bMultiQuality)
{
	TER					terResult		= tesSUCCESS;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	uint64				uRateMax		= 0;

	PaymentNode&	pnPrv			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	PaymentNode&	pnNxt			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	const bool		bPrvAccount		= isSetBit(pnPrv.uFlags, STPathElement::typeAccount);
	const bool		bNxtAccount		= isSetBit(pnNxt.uFlags, STPathElement::typeAccount);

	const uint160&	uCurAccountID	= pnCur.uAccountID;
	const uint160&	uPrvAccountID	= bPrvAccount ? pnPrv.uAccountID : uCurAccountID;
	const uint160&	uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&	uCurrencyID		= pnCur.uCurrencyID;

	uint32			uQualityIn		= uIndex ? rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : QUALITY_ONE;
	uint32			uQualityOut		= uIndex == uLast ? rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : QUALITY_ONE;

	// For bNxtAccount
	const STAmount&	saPrvRedeemReq	= pnPrv.saFwdRedeem;
	STAmount		saPrvRedeemAct(saPrvRedeemReq.getCurrency(), saPrvRedeemReq.getIssuer());

	const STAmount&	saPrvIssueReq	= pnPrv.saFwdIssue;
	STAmount		saPrvIssueAct(saPrvIssueReq.getCurrency(), saPrvIssueReq.getIssuer());

	// For !bPrvAccount
	const STAmount&	saPrvDeliverReq	= pnPrv.saRevDeliver;
	STAmount		saPrvDeliverAct(saPrvDeliverReq.getCurrency(), saPrvDeliverReq.getIssuer());

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount&		saCurRedeemAct	= pnCur.saFwdRedeem;

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount&		saCurIssueAct	= pnCur.saFwdIssue;

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount&		saCurDeliverAct	= pnCur.saFwdDeliver;

	Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd> uIndex=%d/%d saPrvRedeemReq=%s saPrvIssueReq=%s saPrvDeliverReq=%s saCurRedeemReq=%s saCurIssueReq=%s saCurDeliverReq=%s")
		% uIndex
		% uLast
		% saPrvRedeemReq.getFullText()
		% saPrvIssueReq.getFullText()
		% saPrvDeliverReq.getFullText()
		% saCurRedeemReq.getFullText()
		% saCurIssueReq.getFullText()
		% saCurDeliverReq.getFullText());

	// Ripple through account.

	if (bPrvAccount && bNxtAccount)
	{
		if (!uIndex)
		{
			// ^ --> ACCOUNT --> account

			// First node, calculate amount to send.
			// XXX Use stamp/ripple balance
			PaymentNode&	pnCur			= pspCur->vpnNodes[uIndex];

			const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
			STAmount&		saCurRedeemAct	= pnCur.saFwdRedeem;
			const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
			STAmount&		saCurIssueAct	= pnCur.saFwdIssue;

			const STAmount&	saCurSendMaxReq	= pspCur->saInReq;	// Negative for no limit, doing a calculation.
			STAmount&		saCurSendMaxAct = pspCur->saInAct;	// Report to user how much this sends.

			if (saCurRedeemReq)
			{
				// Redeem requested.
				saCurRedeemAct	= saCurRedeemReq.isNegative()
									? saCurRedeemReq
									: std::min(saCurRedeemReq, saCurSendMaxReq);
			}
			else
			{
				saCurRedeemAct	= STAmount(saCurRedeemReq);
			}
			saCurSendMaxAct	= saCurRedeemAct;

			if (saCurIssueReq && (saCurSendMaxReq.isNegative() || saCurSendMaxReq != saCurRedeemAct))
			{
				// Issue requested and not over budget.
				saCurIssueAct	= saCurSendMaxReq.isNegative()
									? saCurIssueReq
									: std::min(saCurSendMaxReq-saCurRedeemAct, saCurIssueReq);
			}
			else
			{
				saCurIssueAct	= STAmount(saCurIssueReq);
			}
			saCurSendMaxAct	+= saCurIssueAct;

			Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: ^ --> ACCOUNT --> account : saCurSendMaxReq=%s saCurRedeemAct=%s saCurIssueReq=%s saCurIssueAct=%s")
				% saCurSendMaxReq.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueReq.getFullText()
				% saCurIssueAct.getFullText());
		}
		else if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $
			Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> $ : uPrvAccountID=%s uCurAccountID=%s saPrvRedeemReq=%s saPrvIssueReq=%s")
				% NewcoinAddress::createHumanAccountID(uPrvAccountID)
				% NewcoinAddress::createHumanAccountID(uCurAccountID)
				% saPrvRedeemReq.getFullText()
				% saPrvIssueReq.getFullText());

			// Last node.  Accept all funds.  Calculate amount actually to credit.

			STAmount&	saCurReceive	= pspCur->saOutAct;

			STAmount	saIssueCrd		= uQualityIn >= QUALITY_ONE
											? saPrvIssueReq													// No fee.
											: STAmount::multiply(saPrvIssueReq, uQualityIn, uCurrencyID, saPrvIssueReq.getIssuer());	// Fee.

			// Amount to credit.
			saCurReceive	= saPrvRedeemReq+saIssueCrd;

			// Actually receive.
			rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq+saPrvIssueReq, false);
		}
		else
		{
			// account --> ACCOUNT --> account
			Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> account"));

			// Previous redeem part 1: redeem -> redeem
			if (saPrvRedeemReq != saPrvRedeemAct)			// Previous wants to redeem. To next must be ok.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct, uRateMax);
			}

			// Previous issue part 1: issue -> redeem
			if (saPrvIssueReq != saPrvIssueAct				// Previous wants to issue.
				&& saCurRedeemReq != saCurRedeemAct)		// Current has more to redeem to next.
			{
				// Rate: quality in : quality out
				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct, uRateMax);
			}

			// Previous redeem part 2: redeem -> issue.
			// wants to redeem and current would and can issue.
			// If redeeming cur to next is done, this implies can issue.
			if (saPrvRedeemReq != saPrvRedeemAct			// Previous still wants to redeem.
				&& saCurRedeemReq == saCurRedeemAct			// Current has no more to redeem to next.
				&& saCurIssueReq)
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct, uRateMax);
			}

			// Previous issue part 2 : issue -> issue
			if (saPrvIssueReq != saPrvIssueAct)				// Previous wants to issue. To next must be ok.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct, uRateMax);
			}

			// Adjust prv --> cur balance : take all inbound
			// XXX Currency must be in amount.
			rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> offer"));

		// redeem -> issue.
		// wants to redeem and current would and can issue.
		// If redeeming cur to next is done, this implies can issue.
		if (saPrvRedeemReq)								// Previous wants to redeem.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct, uRateMax);
		}

		// issue -> issue
		if (saPrvRedeemReq == saPrvRedeemAct			// Previous done redeeming: Previous has no IOUs.
			&& saPrvIssueReq)							// Previous wants to issue. To next must be ok.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct, uRateMax);
		}

		// Adjust prv --> cur balance : take all inbound
		// XXX Currency must be in amount.
		rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $
			Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> $"));

			STAmount&	saCurReceive	= pspCur->saOutAct;

			// Amount to credit.
			saCurReceive	= saPrvDeliverAct;

			// No income balance adjustments necessary.  The paying side inside the offer paid to this account.
		}
		else
		{
			// offer --> ACCOUNT --> account
			Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> account"));

			// deliver -> redeem
			if (saPrvDeliverReq)							// Previous wants to deliver.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct, uRateMax);
			}

			// deliver -> issue
			// Wants to redeem and current would and can issue.
			if (saPrvDeliverReq != saPrvDeliverAct			// Previous still wants to deliver.
				&& saCurRedeemReq == saCurRedeemAct			// Current has more to redeem to next.
				&& saCurIssueReq)							// Current wants issue.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct, saCurIssueAct, uRateMax);
			}

			// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		Log(lsINFO) << boost::str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> offer"));

		if (saPrvDeliverReq									// Previous wants to deliver
			&& saCurIssueReq)								// Current wants issue.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct, uRateMax);
		}

		// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
	}

	return terResult;
}

// Return true, iff lhs has less priority than rhs.
bool PathState::lessPriority(const PathState::pointer& lhs, const PathState::pointer& rhs)
{
	if (lhs->uQuality != rhs->uQuality)
		return lhs->uQuality > rhs->uQuality;	// Bigger is worse.

	// Best quanity is second rank.
	if (lhs->saOutAct != rhs->saOutAct)
		return lhs->saOutAct < rhs->saOutAct;	// Smaller is worse.

	// Path index is third rank.
	return lhs->mIndex > rhs->mIndex;			// Bigger is worse.
}

// Make sure the path delivers to uAccountID: uCurrencyID from uIssuerID.
//
// Rules:
// - Currencies must be converted via an offer.
// - A node names it's output.
// - A ripple nodes output issuer must be the node's account or the next node's account.
// - Offers can only go directly to another offer if the currency and issuer are an exact match.
TER PathState::pushImply(
	const uint160& uAccountID,	// --> Delivering to this account.
	const uint160& uCurrencyID,	// --> Delivering this currency.
	const uint160& uIssuerID)	// --> Delivering this issuer.
{
	const PaymentNode&	pnPrv		= vpnNodes.back();
	TER					terResult	= tesSUCCESS;

	Log(lsINFO) << "pushImply> "
		<< NewcoinAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< " " << NewcoinAddress::createHumanAccountID(uIssuerID);

	if (pnPrv.uCurrencyID != uCurrencyID)
	{
		// Currency is different, need to convert via an offer.

		terResult	= pushNode(
					STPathElement::typeCurrency		// Offer.
					 | STPathElement::typeIssuer,
					ACCOUNT_ONE,	// Placeholder for offers.
					uCurrencyID,	// The offer's output is what is now wanted.
					uIssuerID);

	}

	// For ripple, non-stamps, ensure the issuer is on at least one side of the transaction.
	if (tesSUCCESS == terResult
		&& !!uCurrencyID							// Not stamps.
		&& (pnPrv.uAccountID != uIssuerID			// Previous is not issuing own IOUs.
			&& uAccountID != uIssuerID))			// Current is not receiving own IOUs.
	{
		// Need to ripple through uIssuerID's account.

		terResult	= pushNode(
					STPathElement::typeAccount,
					uIssuerID,						// Intermediate account is the needed issuer.
					uCurrencyID,
					uIssuerID);
	}

	Log(lsINFO) << "pushImply< " << terResult;

	return terResult;
}

// Append a node and insert before it any implied nodes.
// <-- terResult: tesSUCCESS, temBAD_PATH, terNO_LINE
TER PathState::pushNode(
	const int iType,
	const uint160& uAccountID,
	const uint160& uCurrencyID,
	const uint160& uIssuerID)
{
	Log(lsINFO) << "pushNode> "
		<< NewcoinAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< "/" << NewcoinAddress::createHumanAccountID(uIssuerID);
	PaymentNode			pnCur;
	const bool			bFirst		= vpnNodes.empty();
	const PaymentNode&	pnPrv		= bFirst ? PaymentNode() : vpnNodes.back();
	// true, iff node is a ripple account. false, iff node is an offer node.
	const bool			bAccount	= isSetBit(iType, STPathElement::typeAccount);
	// true, iff currency supplied.
	// Currency is specified for the output of the current node.
	const bool			bCurrency	= isSetBit(iType, STPathElement::typeCurrency);
	// Issuer is specified for the output of the current node.
	const bool			bIssuer		= isSetBit(iType, STPathElement::typeIssuer);
	TER					terResult	= tesSUCCESS;

	pnCur.uFlags		= iType;

	if (iType & ~STPathElement::typeValidBits)
	{
		Log(lsINFO) << "pushNode: bad bits.";

		terResult	= temBAD_PATH;
	}
	else if (bAccount)
	{
		// Account link

		pnCur.uAccountID	= uAccountID;
		pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
		pnCur.uIssuerID		= bIssuer ? uIssuerID : uAccountID;
		pnCur.saRevRedeem	= STAmount(uCurrencyID, uAccountID);
		pnCur.saRevIssue	= STAmount(uCurrencyID, uAccountID);

		if (!bFirst)
		{
			// Add required intermediate nodes to deliver to current account.
			terResult	= pushImply(
				pnCur.uAccountID,									// Current account.
				pnCur.uCurrencyID,									// Wanted currency.
				!!pnCur.uCurrencyID ? uAccountID : ACCOUNT_XNS);	// Account as issuer.
		}

		if (tesSUCCESS == terResult && !vpnNodes.empty())
		{
			const PaymentNode&	pnBck		= vpnNodes.back();
			bool				bBckAccount	= isSetBit(pnBck.uFlags, STPathElement::typeAccount);

			if (bBckAccount)
			{
				SLE::pointer	sleRippleState	= mLedger->getSLE(Ledger::getRippleStateIndex(pnBck.uAccountID, pnCur.uAccountID, pnPrv.uCurrencyID));

				if (!sleRippleState)
				{
					Log(lsINFO) << "pushNode: No credit line between "
						<< NewcoinAddress::createHumanAccountID(pnBck.uAccountID)
						<< " and "
						<< NewcoinAddress::createHumanAccountID(pnCur.uAccountID)
						<< " for "
						<< STAmount::createHumanCurrency(pnPrv.uCurrencyID)
						<< "." ;

					Log(lsINFO) << getJson();

					terResult	= terNO_LINE;
				}
				else
				{
					Log(lsINFO) << "pushNode: Credit line found between "
						<< NewcoinAddress::createHumanAccountID(pnBck.uAccountID)
						<< " and "
						<< NewcoinAddress::createHumanAccountID(pnCur.uAccountID)
						<< " for "
						<< STAmount::createHumanCurrency(pnPrv.uCurrencyID)
						<< "." ;
				}
			}
		}

		if (tesSUCCESS == terResult)
			vpnNodes.push_back(pnCur);
	}
	else
	{
		// Offer link
		// Offers bridge a change in currency & issuer or just a change in issuer.
		pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
		pnCur.uIssuerID		= bIssuer ? uIssuerID : pnCur.uAccountID;
		pnCur.saRateMax		= saZero;

		if (!!pnPrv.uAccountID)
		{
			// Previous is an account.

			// Insert intermediary issuer account if needed.
			terResult	= pushImply(
				!!pnPrv.uCurrencyID
					? ACCOUNT_ONE	// Rippling, but offer's don't have an account.
					: ACCOUNT_XNS,
				pnPrv.uCurrencyID,
				pnPrv.uIssuerID);
		}

		if (tesSUCCESS == terResult)
		{
			vpnNodes.push_back(pnCur);
		}
	}
	Log(lsINFO) << "pushNode< " << terResult;

	return terResult;
}

PathState::PathState(
	Ledger::ref				lpLedger,
	const int				iIndex,
	const LedgerEntrySet&	lesSource,
	const STPath&			spSourcePath,
	const uint160&			uReceiverID,
	const uint160&			uSenderID,
	const STAmount&			saSend,
	const STAmount&			saSendMax
	)
	: mLedger(lpLedger), mIndex(iIndex), uQuality(0)
{
	const uint160	uInCurrencyID	= saSendMax.getCurrency();
	const uint160	uOutCurrencyID	= saSend.getCurrency();
	const uint160	uInIssuerID		= !!uInCurrencyID ? saSendMax.getIssuer() : ACCOUNT_XNS;
	const uint160	uOutIssuerID	= !!uOutCurrencyID ? saSend.getIssuer() : ACCOUNT_XNS;

	lesEntries				= lesSource.duplicate();

	saInReq					= saSendMax;
	saOutReq				= saSend;

	// Push sending node.
	terStatus	= pushNode(
		STPathElement::typeAccount
			| STPathElement::typeCurrency
			| STPathElement::typeIssuer,
		uSenderID,
		uInCurrencyID,
		uInIssuerID);

	BOOST_FOREACH(const STPathElement& speElement, spSourcePath)
	{
		if (tesSUCCESS == terStatus)
			terStatus	= pushNode(speElement.getNodeType(), speElement.getAccountID(), speElement.getCurrency(), speElement.getIssuerID());
	}

	if (tesSUCCESS == terStatus)
	{
		// Create receiver node.

		terStatus	= pushImply(uReceiverID, uOutCurrencyID, uOutIssuerID);
		if (tesSUCCESS == terStatus)
		{
			terStatus	= pushNode(
				STPathElement::typeAccount						// Last node is always an account.
					| STPathElement::typeCurrency
					| STPathElement::typeIssuer,
				uReceiverID,									// Receive to output
				uOutCurrencyID,									// Desired currency
				uOutIssuerID);
		}
	}

	if (tesSUCCESS == terStatus)
	{
		// Look for first mention of source in nodes and detect loops.
		// Note: The output is not allowed to be a source.

		const unsigned int	uNodes	= vpnNodes.size();

		for (unsigned int uIndex = 0; tesSUCCESS == terStatus && uIndex != uNodes; ++uIndex)
		{
			const PaymentNode&	pnCur	= vpnNodes[uIndex];

			if (!!pnCur.uAccountID)
			{
				// Source is a ripple line
				nothing();
			}
			else if (!umForward.insert(std::make_pair(boost::make_tuple(pnCur.uAccountID, pnCur.uCurrencyID, pnCur.uIssuerID), uIndex)).second)
			{
				// Failed to insert. Have a loop.
				Log(lsINFO) << boost::str(boost::format("PathState: loop detected: %s")
					% getJson());

				terStatus	= temBAD_PATH_LOOP;
			}
		}
	}

	Log(lsINFO) << boost::str(boost::format("PathState: in=%s/%s out=%s/%s %s")
		% STAmount::createHumanCurrency(uInCurrencyID)
		% NewcoinAddress::createHumanAccountID(uInIssuerID)
		% STAmount::createHumanCurrency(uOutCurrencyID)
		% NewcoinAddress::createHumanAccountID(uOutIssuerID)
		% getJson());
}

Json::Value	PathState::getJson() const
{
	Json::Value	jvPathState(Json::objectValue);
	Json::Value	jvNodes(Json::arrayValue);

	BOOST_FOREACH(const PaymentNode& pnNode, vpnNodes)
	{
		Json::Value	jvNode(Json::objectValue);

		Json::Value	jvFlags(Json::arrayValue);

		if (pnNode.uFlags & STPathElement::typeAccount)
			jvFlags.append("account");

		jvNode["flags"]	= jvFlags;

		if (pnNode.uFlags & STPathElement::typeAccount)
			jvNode["account"]	= NewcoinAddress::createHumanAccountID(pnNode.uAccountID);

		if (!!pnNode.uCurrencyID)
			jvNode["currency"]	= STAmount::createHumanCurrency(pnNode.uCurrencyID);

		if (!!pnNode.uIssuerID)
			jvNode["issuer"]	= NewcoinAddress::createHumanAccountID(pnNode.uIssuerID);

		// if (pnNode.saRevRedeem)
			jvNode["rev_redeem"]	= pnNode.saRevRedeem.getFullText();

		// if (pnNode.saRevIssue)
			jvNode["rev_issue"]		= pnNode.saRevIssue.getFullText();

		// if (pnNode.saRevDeliver)
			jvNode["rev_deliver"]	= pnNode.saRevDeliver.getFullText();

		// if (pnNode.saFwdRedeem)
			jvNode["fwd_redeem"]	= pnNode.saFwdRedeem.getFullText();

		// if (pnNode.saFwdIssue)
			jvNode["fwd_issue"]		= pnNode.saFwdIssue.getFullText();

		// if (pnNode.saFwdDeliver)
			jvNode["fwd_deliver"]	= pnNode.saFwdDeliver.getFullText();

		jvNodes.append(jvNode);
	}

	jvPathState["status"]	= terStatus;
	jvPathState["index"]	= mIndex;
	jvPathState["nodes"]	= jvNodes;

	if (saInReq)
		jvPathState["in_req"]	= saInReq.getJson(0);

	if (saInAct)
		jvPathState["in_act"]	= saInAct.getJson(0);

	if (saOutReq)
		jvPathState["out_req"]	= saOutReq.getJson(0);

	if (saOutAct)
		jvPathState["out_act"]	= saOutAct.getJson(0);

	if (uQuality)
		jvPathState["uQuality"]	= Json::Value::UInt(uQuality);

	return jvPathState;
}

TER TransactionEngine::calcNodeFwd(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality)
{
	const PaymentNode&		pnCur		= pspCur->vpnNodes[uIndex];
	const bool				bCurAccount	= isSetBit(pnCur.uFlags,  STPathElement::typeAccount);

	Log(lsINFO) << boost::str(boost::format("calcNodeFwd> uIndex=%d") % uIndex);

	TER						terResult	= bCurAccount
											? calcNodeAccountFwd(uIndex, pspCur, bMultiQuality)
											: calcNodeOfferFwd(uIndex, pspCur, bMultiQuality);

	if (tesSUCCESS == terResult && uIndex + 1 != pspCur->vpnNodes.size())
	{
		terResult	= calcNodeFwd(uIndex+1, pspCur, bMultiQuality);
	}

	Log(lsINFO) << boost::str(boost::format("calcNodeFwd< uIndex=%d terResult=%d") % uIndex % terResult);

	return terResult;
}

// Calculate a node and its previous nodes.
// From the destination work in reverse towards the source calculating how much must be asked for.
// Then work forward, figuring out how much can actually be delivered.
// <-- terResult: tesSUCCESS or tepPATH_DRY
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
TER TransactionEngine::calcNodeRev(const unsigned int uIndex, const PathState::pointer& pspCur, const bool bMultiQuality)
{
	PaymentNode&	pnCur		= pspCur->vpnNodes[uIndex];
	const bool		bCurAccount	= isSetBit(pnCur.uFlags,  STPathElement::typeAccount);
	TER				terResult;

	// Do current node reverse.
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	STAmount&		saTransferRate	= pnCur.saTransferRate;

		saTransferRate	= STAmount::saFromRate(rippleTransferRate(uCurIssuerID));

	Log(lsINFO) << boost::str(boost::format("calcNodeRev> uIndex=%d uIssuerID=%s saTransferRate=%s")
		% uIndex
		% NewcoinAddress::createHumanAccountID(uCurIssuerID)
		% saTransferRate.getFullText());

	terResult	= bCurAccount
					? calcNodeAccountRev(uIndex, pspCur, bMultiQuality)
					: calcNodeOfferRev(uIndex, pspCur, bMultiQuality);

	// Do previous.
	if (tesSUCCESS != terResult)
	{
		// Error, don't continue.
		nothing();
	}
	else if (uIndex)
	{
		// Continue in reverse.

		terResult	= calcNodeRev(uIndex-1, pspCur, bMultiQuality);
	}

	Log(lsINFO) << boost::str(boost::format("calcNodeRev< uIndex=%d terResult=%s/%d") % uIndex % transToken(terResult) % terResult);

	return terResult;
}

// Calculate the next increment of a path.
// The increment is what can satisfy a portion or all of the requested output at the best quality.
// <-- pspCur->uQuality
void TransactionEngine::pathNext(const PathState::pointer& pspCur, const int iPaths, const LedgerEntrySet& lesCheckpoint)
{
	// The next state is what is available in preference order.
	// This is calculated when referenced accounts changed.
	const bool			bMultiQuality	= iPaths == 1;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	Log(lsINFO) << "Path In: " << pspCur->getJson();

	assert(pspCur->vpnNodes.size() >= 2);

	pspCur->vUnfundedBecame.clear();
	pspCur->umReverse.clear();

	mNodes	= lesCheckpoint;					// Restore from checkpoint.
	mNodes.bumpSeq();							// Begin ledger varance.

	pspCur->terStatus	= calcNodeRev(uLast, pspCur, bMultiQuality);

	Log(lsINFO) << "Path after reverse: " << pspCur->getJson();

	if (tesSUCCESS == pspCur->terStatus)
	{
		// Do forward.
		mNodes	= lesCheckpoint;					// Restore from checkpoint.
		mNodes.bumpSeq();							// Begin ledger varance.

		pspCur->terStatus	= calcNodeFwd(0, pspCur, bMultiQuality);

		pspCur->uQuality	= tesSUCCESS == pspCur->terStatus
								? STAmount::getRate(pspCur->saOutAct, pspCur->saInAct)	// Calculate relative quality.
								: 0;													// Mark path as inactive.

		Log(lsINFO) << "Path after forward: " << pspCur->getJson();
	}
}

// XXX Need to audit for things like setting accountID not having memory.
TER TransactionEngine::doPayment(const SerializedTransaction& txn)
{
	// Ripple if source or destination is non-native or if there are paths.
	const uint32	uTxFlags		= txn.getFlags();
	const bool		bCreate			= isSetBit(uTxFlags, tfCreateAccount);
	const bool		bPartialPayment	= isSetBit(uTxFlags, tfPartialPayment);
	const bool		bLimitQuality	= isSetBit(uTxFlags, tfLimitQuality);
	const bool		bNoRippleDirect	= isSetBit(uTxFlags, tfNoRippleDirect);
	const bool		bPaths			= txn.getITFieldPresent(sfPaths);
	const bool		bMax			= txn.getITFieldPresent(sfSendMax);
	const uint160	uDstAccountID	= txn.getITFieldAccount(sfDestination);
	const STAmount	saDstAmount		= txn.getITFieldAmount(sfAmount);
	const STAmount	saMaxAmount		= bMax ? txn.getITFieldAmount(sfSendMax) : saDstAmount;
	const uint160	uSrcCurrency	= saMaxAmount.getCurrency();
	const uint160	uDstCurrency	= saDstAmount.getCurrency();

	Log(lsINFO) << boost::str(boost::format("doPayment> saMaxAmount=%s saDstAmount=%s")
		% saMaxAmount.getFullText()
		% saDstAmount.getFullText());

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doPayment: Invalid transaction: Payment destination account not specifed.";

		return temDST_NEEDED;
	}
	else if (!saDstAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad amount: " << saDstAmount.getHumanCurrency() << " " << saDstAmount.getText();

		return temBAD_AMOUNT;
	}
	else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Invalid transaction: Redunant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% mTxnAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return temREDUNDANT;
	}
	else if (bMax
		&& ((saMaxAmount == saDstAmount && saMaxAmount.getCurrency() == saDstAmount.getCurrency())
			|| (saDstAmount.isNative() && saMaxAmount.isNative())))
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad SendMax.";

		return temINVALID;
	}

	SLE::pointer	sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		if (bCreate && !saDstAmount.isNative())
		{
			// This restriction could be relaxed.
			Log(lsINFO) << "doPayment: Invalid transaction: Create account may only fund XNS.";

			return temCREATEXNS;
		}
		else if (!bCreate)
		{
			Log(lsINFO) << "doPayment: Delay transaction: Destination account does not exist.";

			return terNO_DST;
		}

		// Create the account.
		sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

		sleDst->setIFieldAccount(sfAccount, uDstAccountID);
		sleDst->setIFieldU32(sfSequence, 1);
	}
	else
	{
		entryModify(sleDst);
	}

	// XXX Should bMax be sufficient to imply ripple?
	const bool		bRipple	= bPaths || bMax || !saDstAmount.isNative();

	if (!bRipple)
	{
		// Direct XNS payment.
		STAmount	saSrcXNSBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);

		if (saSrcXNSBalance < saDstAmount)
		{
			// Transaction might succeed, if applied in a different order.
			Log(lsINFO) << "doPayment: Delay transaction: Insufficent funds.";

			return terUNFUNDED;
		}

		mTxnAccount->setIFieldAmount(sfBalance, saSrcXNSBalance - saDstAmount);
		sleDst->setIFieldAmount(sfBalance, sleDst->getIValueFieldAmount(sfBalance) + saDstAmount);

		return tesSUCCESS;
	}

	//
	// Ripple payment
	//

	STPathSet	spsPaths = txn.getITFieldPathSet(sfPaths);

	if (bNoRippleDirect && spsPaths.isEmpty())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: No paths and direct ripple not allowed.";

		return temRIPPLE_EMPTY;
	}

	// XXX Skip check if final processing.
	if (spsPaths.getPathCount() > RIPPLE_PATHS_MAX)
	{
		return telBAD_PATH_COUNT;
	}

	// Incrementally search paths.
	std::vector<PathState::pointer>	vpsPaths;

	TER				terResult	= temUNCERTAIN;

	if (!bNoRippleDirect)
	{
		// Direct path.
		// XXX Might also make a stamp bridge by default.
		Log(lsINFO) << "doPayment: Build direct:";

		PathState::pointer	pspDirect	= PathState::createPathState(
			mLedger,
			vpsPaths.size(),
			mNodes,
			STPath(),
			uDstAccountID,
			mTxnAccountID,
			saDstAmount,
			saMaxAmount);

		if (pspDirect)
		{
			// Return if malformed.
			if (pspDirect->terStatus >= temMALFORMED && pspDirect->terStatus < tefFAILURE)
				return pspDirect->terStatus;

			if (tesSUCCESS == pspDirect->terStatus)
			{
				// Had a success.
				terResult	= tesSUCCESS;

				vpsPaths.push_back(pspDirect);
			}
		}
	}

	Log(lsINFO) << "doPayment: Paths in set: " << spsPaths.getPathCount();

	BOOST_FOREACH(const STPath& spPath, spsPaths)
	{
		Log(lsINFO) << "doPayment: Build path:";

		PathState::pointer	pspExpanded	= PathState::createPathState(
			mLedger,
			vpsPaths.size(),
			mNodes,
			spPath,
			uDstAccountID,
			mTxnAccountID,
			saDstAmount,
			saMaxAmount);

		if (pspExpanded)
		{
			// Return if malformed.
			if (pspExpanded->terStatus >= temMALFORMED && pspExpanded->terStatus < tefFAILURE)
				return pspExpanded->terStatus;

			if (tesSUCCESS == pspExpanded->terStatus)
			{
				// Had a success.
				terResult	= tesSUCCESS;
			}

			vpsPaths.push_back(pspExpanded);
		}
	}

	if (vpsPaths.empty())
	{
		return tefEXCEPTION;
	}
	else if (tesSUCCESS != terResult)
	{
		// No path successes.

		return vpsPaths[0]->terStatus;
	}
	else
	{
		terResult	= temUNCERTAIN;
	}

	STAmount				saPaid;
	STAmount				saWanted;
	const LedgerEntrySet	lesBase			= mNodes;										// Checkpoint with just fees paid.
	const uint64			uQualityLimit	= bLimitQuality ? STAmount::getRate(saDstAmount, saMaxAmount) : 0;

	while (temUNCERTAIN == terResult)
	{
		PathState::pointer		pspBest;
		const LedgerEntrySet	lesCheckpoint	= mNodes;

		// Find the best path.
		BOOST_FOREACH(PathState::pointer& pspCur, vpsPaths)
		{
			pathNext(pspCur, vpsPaths.size(), lesCheckpoint);						// Compute increment.

			if ((!bLimitQuality || pspCur->uQuality <= uQualityLimit)				// Quality is not limted or increment has allowed quality.
				|| !pspBest															// Best is not yet set.
				|| (pspCur->uQuality && PathState::lessPriority(pspBest, pspCur)))	// Current is better than set.
			{
				mNodes.swapWith(pspCur->lesEntries);								// For the path, save ledger state.
				pspBest	= pspCur;
			}
		}

		if (pspBest)
		{
			// Apply best path.

			// Record best pass' offers that became unfunded for deletion on success.
			mvUnfundedBecame.insert(mvUnfundedBecame.end(), pspBest->vUnfundedBecame.begin(), pspBest->vUnfundedBecame.end());

			// Record best pass' LedgerEntrySet to build off of and potentially return.
			mNodes.swapWith(pspBest->lesEntries);

			// Figure out if done.
			if (temUNCERTAIN == terResult && saPaid == saWanted)
			{
				terResult	= tesSUCCESS;
			}
			else
			{
				// Prepare for next pass.

				// Merge best pass' umReverse.
				mumSource.insert(pspBest->umReverse.begin(), pspBest->umReverse.end());
			}
		}
		// Not done and ran out of paths.
		else if (!bPartialPayment)
		{
			// Partial payment not allowed.
			terResult	= tepPATH_PARTIAL;
			mNodes		= lesBase;				// Revert to just fees charged.
		}
		// Partial payment ok.
		else if (!saPaid)
		{
			// No payment at all.
			terResult	= tepPATH_DRY;
			mNodes		= lesBase;				// Revert to just fees charged.
		}
		else
		{
			terResult	= tesSUCCESS;
		}
	}

	if (tesSUCCESS == terResult)
	{
		// Delete became unfunded offers.
		BOOST_FOREACH(const uint256& uOfferIndex, mvUnfundedBecame)
		{
			if (tesSUCCESS == terResult)
				terResult = offerDelete(uOfferIndex);
		}
	}

	// Delete found unfunded offers.
	BOOST_FOREACH(const uint256& uOfferIndex, musUnfundedFound)
	{
		if (tesSUCCESS == terResult)
			terResult = offerDelete(uOfferIndex);
	}

	std::string	strToken;
	std::string	strHuman;

	if (transResultInfo(terResult, strToken, strHuman))
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: %s: %s") % strToken % strHuman);
	}
	else
	{
		assert(false);
	}

	return terResult;
}

TER TransactionEngine::doWalletAdd(const SerializedTransaction& txn)
{
	std::cerr << "WalletAdd>" << std::endl;

	const std::vector<unsigned char>	vucPubKey		= txn.getITFieldVL(sfPubKey);
	const std::vector<unsigned char>	vucSignature	= txn.getITFieldVL(sfSignature);
	const uint160						uAuthKeyID		= txn.getITFieldAccount(sfAuthorizedKey);
	const NewcoinAddress				naMasterPubKey	= NewcoinAddress::createAccountPublic(vucPubKey);
	const uint160						uDstAccountID	= naMasterPubKey.getAccountID();

	if (!naMasterPubKey.accountPublicVerify(Serializer::getSHA512Half(uAuthKeyID.begin(), uAuthKeyID.size()), vucSignature))
	{
		std::cerr << "WalletAdd: unauthorized: bad signature " << std::endl;

		return tefBAD_ADD_AUTH;
	}

	SLE::pointer		sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tefCREATED;
	}

	STAmount			saAmount		= txn.getITFieldAmount(sfAmount);
	STAmount			saSrcBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);

	if (saSrcBalance < saAmount)
	{
		std::cerr
			<< boost::str(boost::format("WalletAdd: Delay transaction: insufficent balance: balance=%s amount=%s")
				% saSrcBalance.getText()
				% saAmount.getText())
			<< std::endl;

		return terUNFUNDED;
	}

	// Deduct initial balance from source account.
	mTxnAccount->setIFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	sleDst->setIFieldAccount(sfAccount, uDstAccountID);
	sleDst->setIFieldU32(sfSequence, 1);
	sleDst->setIFieldAmount(sfBalance, saAmount);
	sleDst->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	std::cerr << "WalletAdd<" << std::endl;

	return tesSUCCESS;
}

TER TransactionEngine::doInvoice(const SerializedTransaction& txn)
{
	return temUNKNOWN;
}

// Take as much as possible. Adjusts account balances. Charges fees on top to taker.
// -->   uBookBase: The order book to take against.
// --> saTakerPays: What the taker offers (w/ issuer)
// --> saTakerGets: What the taker wanted (w/ issuer)
// <-- saTakerPaid: What taker paid not including fees. To reduce an offer.
// <--  saTakerGot: What taker got not including fees. To reduce an offer.
// <--   terResult: tesSUCCESS or terNO_ACCOUNT
// XXX: Fees should be paid by the source of the currency.
TER TransactionEngine::takeOffers(
	bool				bPassive,
	const uint256&		uBookBase,
	const uint160&		uTakerAccountID,
	const SLE::pointer&	sleTakerAccount,
	const STAmount&		saTakerPays,
	const STAmount&		saTakerGets,
	STAmount&			saTakerPaid,
	STAmount&			saTakerGot)
{
	assert(saTakerPays && saTakerGets);

	Log(lsINFO) << "takeOffers: against book: " << uBookBase.ToString();

	uint256					uTipIndex			= uBookBase;
	const uint256			uBookEnd			= Ledger::getQualityNext(uBookBase);
	const uint64			uTakeQuality		= STAmount::getRate(saTakerGets, saTakerPays);
	const uint160			uTakerPaysAccountID	= saTakerPays.getIssuer();
	const uint160			uTakerGetsAccountID	= saTakerGets.getIssuer();
	TER						terResult			= temUNCERTAIN;

	boost::unordered_set<uint256>	usOfferUnfundedFound;	// Offers found unfunded.
	boost::unordered_set<uint256>	usOfferUnfundedBecame;	// Offers that became unfunded.
	boost::unordered_set<uint160>	usAccountTouched;		// Accounts touched.

	saTakerPaid	= 0;
	saTakerGot	= 0;

	while (temUNCERTAIN == terResult)
	{
		SLE::pointer	sleOfferDir;
		uint64			uTipQuality;

		// Figure out next offer to take, if needed.
		if (saTakerGets != saTakerGot && saTakerPays != saTakerPaid)
		{
			// Taker has needs.

			sleOfferDir		= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uTipIndex, uBookEnd));
			if (sleOfferDir)
			{
				Log(lsINFO) << "takeOffers: possible counter offer found";

				uTipIndex		= sleOfferDir->getIndex();
				uTipQuality		= Ledger::getQuality(uTipIndex);
			}
			else
			{
				Log(lsINFO) << "takeOffers: counter offer book is empty: "
					<< uTipIndex.ToString()
					<< " ... "
					<< uBookEnd.ToString();
			}
		}

		if (!sleOfferDir									// No offer directory to take.
			|| uTakeQuality < uTipQuality					// No offer's of sufficient quality available.
			|| (bPassive && uTakeQuality == uTipQuality))
		{
			// Done.
			Log(lsINFO) << "takeOffers: done";

			terResult	= tesSUCCESS;
		}
		else
		{
			// Have an offer directory to consider.
			Log(lsINFO) << "takeOffers: considering dir : " << sleOfferDir->getJson(0);

			SLE::pointer	sleBookNode;
			unsigned int	uBookEntry;
			uint256			uOfferIndex;

			dirFirst(uTipIndex, sleBookNode, uBookEntry, uOfferIndex);

			SLE::pointer	sleOffer		= entryCache(ltOFFER, uOfferIndex);

			Log(lsINFO) << "takeOffers: considering offer : " << sleOffer->getJson(0);

			const uint160	uOfferOwnerID	= sleOffer->getIValueFieldAccount(sfAccount).getAccountID();
			STAmount		saOfferPays		= sleOffer->getIValueFieldAmount(sfTakerGets);
			STAmount		saOfferGets		= sleOffer->getIValueFieldAmount(sfTakerPays);

			if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
			{
				// Offer is expired. Expired offers are considered unfunded. Delete it.
				Log(lsINFO) << "takeOffers: encountered expired offer";

				usOfferUnfundedFound.insert(uOfferIndex);
			}
			else if (uOfferOwnerID == uTakerAccountID)
			{
				// Would take own offer. Consider old offer expired. Delete it.
				Log(lsINFO) << "takeOffers: encountered taker's own old offer";

				usOfferUnfundedFound.insert(uOfferIndex);
			}
			else
			{
				// Get offer funds available.

				Log(lsINFO) << "takeOffers: saOfferPays=" << saOfferPays.getFullText();

				STAmount		saOfferFunds	= accountFunds(uOfferOwnerID, saOfferPays);
				STAmount		saTakerFunds	= accountFunds(uTakerAccountID, saTakerPays);
				SLE::pointer	sleOfferAccount;	// Owner of offer.

				if (!saOfferFunds.isPositive())
				{
					// Offer is unfunded, possibly due to previous balance action.
					Log(lsINFO) << "takeOffers: offer unfunded: delete";

					boost::unordered_set<uint160>::iterator	account	= usAccountTouched.find(uOfferOwnerID);
					if (account != usAccountTouched.end())
					{
						// Previously touched account.
						usOfferUnfundedBecame.insert(uOfferIndex);	// Delete unfunded offer on success.
					}
					else
					{
						// Never touched source account.
						usOfferUnfundedFound.insert(uOfferIndex);	// Delete found unfunded offer when possible.
					}
				}
				else
				{
					STAmount	saPay		= saTakerPays - saTakerPaid;
						if (saTakerFunds < saPay)
							saPay	= saTakerFunds;
					STAmount	saSubTakerPaid;
					STAmount	saSubTakerGot;

					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPaid: " << saTakerPaid.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:   saTakerFunds: " << saTakerFunds.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:   saOfferFunds: " << saOfferFunds.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:          saPay: " << saPay.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saOfferPays: " << saOfferPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saOfferGets: " << saOfferGets.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerPays: " << saTakerPays.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:    saTakerGets: " << saTakerGets.getFullText();

					bool	bOfferDelete	= STAmount::applyOffer(
						saOfferFunds,
						saPay,				// Driver XXX need to account for fees.
						saOfferPays,
						saOfferGets,
						saTakerPays,
						saTakerGets,
						saSubTakerPaid,
						saSubTakerGot);

					Log(lsINFO) << "takeOffers: applyOffer: saSubTakerPaid: " << saSubTakerPaid.getFullText();
					Log(lsINFO) << "takeOffers: applyOffer:  saSubTakerGot: " << saSubTakerGot.getFullText();

					// Adjust offer

					// Offer owner will pay less.  Subtract what taker just got.
					sleOffer->setIFieldAmount(sfTakerGets, saOfferPays -= saSubTakerGot);

					// Offer owner will get less.  Subtract what owner just paid.
					sleOffer->setIFieldAmount(sfTakerPays, saOfferGets -= saSubTakerPaid);

					entryModify(sleOffer);

					if (bOfferDelete)
					{
						// Offer now fully claimed or now unfunded.
						Log(lsINFO) << "takeOffers: offer claimed: delete";

						usOfferUnfundedBecame.insert(uOfferIndex);	// Delete unfunded offer on success.

						// Offer owner's account is no longer pristine.
						usAccountTouched.insert(uOfferOwnerID);
					}
					else
					{
						Log(lsINFO) << "takeOffers: offer partial claim.";
					}

					// Offer owner pays taker.
					saSubTakerGot.setIssuer(uTakerGetsAccountID);	// XXX Move this earlier?

					accountSend(uOfferOwnerID, uTakerAccountID, saSubTakerGot);

					saTakerGot	+= saSubTakerGot;

					// Taker pays offer owner.
					saSubTakerPaid.setIssuer(uTakerPaysAccountID);

					accountSend(uTakerAccountID, uOfferOwnerID, saSubTakerPaid);

					saTakerPaid	+= saSubTakerPaid;
				}
			}
		}
	}

	// On storing meta data, delete offers that were found unfunded to prevent encountering them in future.
	if (tesSUCCESS == terResult)
	{
		BOOST_FOREACH(const uint256& uOfferIndex, usOfferUnfundedFound)
		{
			terResult	= offerDelete(uOfferIndex);
			if (tesSUCCESS != terResult)
				break;
		}
	}

	if (tesSUCCESS == terResult)
	{
		// On success, delete offers that became unfunded.
		BOOST_FOREACH(const uint256& uOfferIndex, usOfferUnfundedBecame)
		{
			terResult	= offerDelete(uOfferIndex);
			if (tesSUCCESS != terResult)
				break;
		}
	}

	return terResult;
}

TER TransactionEngine::doOfferCreate(const SerializedTransaction& txn)
{
Log(lsWARNING) << "doOfferCreate> " << txn.getJson(0);
	const uint32			txFlags			= txn.getFlags();
	const bool				bPassive		= isSetBit(txFlags, tfPassive);
	STAmount				saTakerPays		= txn.getITFieldAmount(sfTakerPays);
	STAmount				saTakerGets		= txn.getITFieldAmount(sfTakerGets);
Log(lsWARNING) << "doOfferCreate: saTakerPays=" << saTakerPays.getFullText();
Log(lsWARNING) << "doOfferCreate: saTakerGets=" << saTakerGets.getFullText();
	const uint160			uPaysIssuerID	= saTakerPays.getIssuer();
	const uint160			uGetsIssuerID	= saTakerGets.getIssuer();
	const uint32			uExpiration		= txn.getITFieldU32(sfExpiration);
	const bool				bHaveExpiration	= txn.getITFieldPresent(sfExpiration);
	const uint32			uSequence		= txn.getSequence();

	const uint256			uLedgerIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer			sleOffer		= entryCreate(ltOFFER, uLedgerIndex);

	Log(lsINFO) << "doOfferCreate: Creating offer node: " << uLedgerIndex.ToString() << " uSequence=" << uSequence;

	const uint160			uPaysCurrency	= saTakerPays.getCurrency();
	const uint160			uGetsCurrency	= saTakerGets.getCurrency();
	const uint64			uRate			= STAmount::getRate(saTakerGets, saTakerPays);

	TER						terResult		= tesSUCCESS;
	uint256					uDirectory;		// Delete hints.
	uint64					uOwnerNode;
	uint64					uBookNode;

	if (bHaveExpiration && !uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad expiration";

		terResult	= temBAD_EXPIRATION;
	}
	else if (bHaveExpiration && mLedger->getParentCloseTimeNC() >= uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Expired transaction: offer expired";

		// XXX CHARGE FEE ONLY.
		terResult	= tesSUCCESS;
	}
	else if (saTakerPays.isNative() && saTakerGets.isNative())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: XNS for XNS";

		terResult	= temBAD_OFFER;
	}
	else if (!saTakerPays || !saTakerGets)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad amount";

		terResult	= temBAD_OFFER;
	}
	else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: redundant offer";

		terResult	= temREDUNDANT;
	}
	else if (saTakerPays.isNative() != !uPaysIssuerID || saTakerGets.isNative() != !uGetsIssuerID)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad issuer";

		terResult	= temBAD_ISSUER;
	}
	else if (!accountFunds(mTxnAccountID, saTakerGets).isPositive())
	{
		Log(lsWARNING) << "doOfferCreate: delay: Offers must be at least partially funded.";

		terResult	= terUNFUNDED;
	}

	if (tesSUCCESS == terResult && !saTakerPays.isNative())
	{
		SLE::pointer		sleTakerPays	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uPaysIssuerID));

		if (!sleTakerPays)
		{
			Log(lsWARNING) << "doOfferCreate: delay: can't receive IOUs from non-existant issuer: " << NewcoinAddress::createHumanAccountID(uPaysIssuerID);

			terResult	= terNO_ACCOUNT;
		}
	}

	if (tesSUCCESS == terResult)
	{
		STAmount		saOfferPaid;
		STAmount		saOfferGot;
		const uint256	uTakeBookBase	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

		Log(lsINFO) << boost::str(boost::format("doOfferCreate: take against book: %s : %s/%s -> %s/%s")
			% uTakeBookBase.ToString()
			% saTakerGets.getHumanCurrency()
			% NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer())
			% saTakerPays.getHumanCurrency()
			% NewcoinAddress::createHumanAccountID(saTakerPays.getIssuer()));

		// Take using the parameters of the offer.
		terResult	= takeOffers(
						bPassive,
						uTakeBookBase,
						mTxnAccountID,
						mTxnAccount,
						saTakerGets,
						saTakerPays,
						saOfferPaid,	// How much was spent.
						saOfferGot		// How much was got.
					);

		Log(lsWARNING) << "doOfferCreate: takeOffers=" << terResult;
		Log(lsWARNING) << "doOfferCreate: takeOffers: saOfferPaid=" << saOfferPaid.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers:  saOfferGot=" << saOfferGot.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
		Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << saTakerGets.getFullText();

		if (tesSUCCESS == terResult)
		{
			saTakerPays		-= saOfferGot;				// Reduce payin from takers by what offer just got.
			saTakerGets		-= saOfferPaid;				// Reduce payout to takers by what srcAccount just paid.
		}
	}

	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerPays=" << saTakerPays.getFullText();
	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << saTakerGets.getFullText();
	Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets=" << NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer());
	Log(lsWARNING) << "doOfferCreate: takeOffers: mTxnAccountID=" << NewcoinAddress::createHumanAccountID(mTxnAccountID);
	Log(lsWARNING) << "doOfferCreate: takeOffers:         funds=" << accountFunds(mTxnAccountID, saTakerGets).getFullText();

	// Log(lsWARNING) << "doOfferCreate: takeOffers: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
	// Log(lsWARNING) << "doOfferCreate: takeOffers: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);

	if (tesSUCCESS == terResult
		&& saTakerPays												// Still wanting something.
		&& saTakerGets												// Still offering something.
		&& accountFunds(mTxnAccountID, saTakerGets).isPositive())	// Still funded.
	{
		// We need to place the remainder of the offer into its order book.

		// Add offer to owner's directory.
		terResult	= dirAdd(uOwnerNode, Ledger::getOwnerDirIndex(mTxnAccountID), uLedgerIndex);

		if (tesSUCCESS == terResult)
		{
			uint256	uBookBase	= Ledger::getBookBase(uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID);

			Log(lsINFO) << boost::str(boost::format("doOfferCreate: adding to book: %s : %s/%s -> %s/%s")
				% uBookBase.ToString()
				% saTakerPays.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerPays.getIssuer())
				% saTakerGets.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer()));

			uDirectory	= Ledger::getQualityIndex(uBookBase, uRate);	// Use original rate.

			// Add offer to order book.
			terResult	= dirAdd(uBookNode, uDirectory, uLedgerIndex);
		}

		if (tesSUCCESS == terResult)
		{
			// Log(lsWARNING) << "doOfferCreate: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
			// Log(lsWARNING) << "doOfferCreate: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);
			// Log(lsWARNING) << "doOfferCreate: saTakerPays.isNative()=" << saTakerPays.isNative();
			// Log(lsWARNING) << "doOfferCreate: saTakerGets.isNative()=" << saTakerGets.isNative();
			// Log(lsWARNING) << "doOfferCreate: uPaysCurrency=" << saTakerPays.getHumanCurrency();
			// Log(lsWARNING) << "doOfferCreate: uGetsCurrency=" << saTakerGets.getHumanCurrency();

			sleOffer->setIFieldAccount(sfAccount, mTxnAccountID);
			sleOffer->setIFieldU32(sfSequence, uSequence);
			sleOffer->setIFieldH256(sfBookDirectory, uDirectory);
			sleOffer->setIFieldAmount(sfTakerPays, saTakerPays);
			sleOffer->setIFieldAmount(sfTakerGets, saTakerGets);
			sleOffer->setIFieldU64(sfOwnerNode, uOwnerNode);
			sleOffer->setIFieldU64(sfBookNode, uBookNode);

			if (uExpiration)
				sleOffer->setIFieldU32(sfExpiration, uExpiration);

			if (bPassive)
				sleOffer->setFlag(lsfPassive);
		}
	}

	return terResult;
}

TER TransactionEngine::doOfferCancel(const SerializedTransaction& txn)
{
	TER				terResult;
	const uint32	uSequence	= txn.getITFieldU32(sfOfferSequence);
	const uint256	uOfferIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer	sleOffer	= entryCache(ltOFFER, uOfferIndex);

	if (sleOffer)
	{
		Log(lsWARNING) << "doOfferCancel: uSequence=" << uSequence;

		terResult	= offerDelete(sleOffer, uOfferIndex, mTxnAccountID);
	}
	else
	{
		Log(lsWARNING) << "doOfferCancel: offer not found: "
			<< NewcoinAddress::createHumanAccountID(mTxnAccountID)
			<< " : " << uSequence
			<< " : " << uOfferIndex.ToString();

		terResult	= terOFFER_NOT_FOUND;
	}

	return terResult;
}

#if 0
// XXX Need to adjust for fees.
// Find offers to satisfy pnDst.
// - Does not adjust any balances as there is at least a forward pass to come.
// --> pnDst.saWanted: currency and amount wanted
// --> pnSrc.saIOURedeem.mCurrency: use this before saIOUIssue, limit to use.
// --> pnSrc.saIOUIssue.mCurrency: use this after saIOURedeem, limit to use.
// <-- pnDst.saReceive
// <-- pnDst.saIOUForgive
// <-- pnDst.saIOUAccept
// <-- terResult : tesSUCCESS = no error and if !bAllowPartial complelely satisfied wanted.
// <-> usOffersDeleteAlways:
// <-> usOffersDeleteOnSuccess:
TER calcOfferFill(PaymentNode& pnSrc, PaymentNode& pnDst, bool bAllowPartial)
{
	TER	terResult;

	if (pnDst.saWanted.isNative())
	{
		// Transfer stamps.

		STAmount	saSrcFunds	= pnSrc.saAccount->accountHolds(pnSrc.saAccount, uint160(0), uint160(0));

		if (saSrcFunds && (bAllowPartial || saSrcFunds > pnDst.saWanted))
		{
			pnSrc.saSend	= min(saSrcFunds, pnDst.saWanted);
			pnDst.saReceive	= pnSrc.saSend;
		}
		else
		{
			terResult	= terINSUF_PATH;
		}
	}
	else
	{
		// Ripple funds.

		// Redeem to limit.
		terResult	= calcOfferFill(
			accountHolds(pnSrc.saAccount, pnDst.saWanted.getCurrency(), pnDst.saWanted.getIssuer()),
			pnSrc.saIOURedeem,
			pnDst.saIOUForgive,
			bAllowPartial);

		if (tesSUCCESS == terResult)
		{
			// Issue to wanted.
			terResult	= calcOfferFill(
				pnDst.saWanted,		// As much as wanted is available, limited by credit limit.
				pnSrc.saIOUIssue,
				pnDst.saIOUAccept,
				bAllowPartial);
		}

		if (tesSUCCESS == terResult && !bAllowPartial)
		{
			STAmount	saTotal	= pnDst.saIOUForgive	+ pnSrc.saIOUAccept;

			if (saTotal != saWanted)
				terResult	= terINSUF_PATH;
		}
	}

	return terResult;
}
#endif

#if 0
// Get the next offer limited by funding.
// - Stop when becomes unfunded.
void TransactionEngine::calcOfferBridgeNext(
	const uint256&		uBookRoot,		// --> Which order book to look in.
	const uint256&		uBookEnd,		// --> Limit of how far to look.
	uint256&			uBookDirIndex,	// <-> Current directory. <-- 0 = no offer available.
	uint64&				uBookDirNode,	// <-> Which node. 0 = first.
	unsigned int&		uBookDirEntry,	// <-> Entry in node. 0 = first.
	STAmount&			saOfferIn,		// <-- How much to pay in, fee inclusive, to get saOfferOut out.
	STAmount&			saOfferOut		// <-- How much offer pays out.
	)
{
	saOfferIn		= 0;	// XXX currency & issuer
	saOfferOut		= 0;	// XXX currency & issuer

	bool			bDone	= false;

	while (!bDone)
	{
		uint256			uOfferIndex;

		// Get uOfferIndex.
		dirNext(uBookRoot, uBookEnd, uBookDirIndex, uBookDirNode, uBookDirEntry, uOfferIndex);

		SLE::pointer	sleOffer		= entryCache(ltOFFER, uOfferIndex);

		uint160			uOfferOwnerID	= sleOffer->getIValueFieldAccount(sfAccount).getAccountID();
		STAmount		saOfferPays		= sleOffer->getIValueFieldAmount(sfTakerGets);
		STAmount		saOfferGets		= sleOffer->getIValueFieldAmount(sfTakerPays);

		if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
		{
			// Offer is expired.
			Log(lsINFO) << "calcOfferFirst: encountered expired offer";
		}
		else
		{
			STAmount		saOfferFunds	= accountFunds(uOfferOwnerID, saOfferPays);
			// Outbound fees are paid by offer owner.
			// XXX Calculate outbound fee rate.

			if (saOfferPays.isNative())
			{
				// No additional fees for stamps.

				nothing();
			}
			else if (saOfferPays.getIssuer() == uOfferOwnerID)
			{
				// Offerer is issue own IOUs.
				// No fees at this exact point, XXX receiving node may charge a fee.
				// XXX Make sure has a credit line with receiver, limit by credit line.

				nothing();
				// XXX Broken - could be issuing or redeeming or both.
			}
			else
			{
				// Offer must be redeeming IOUs.

				// No additional 
				// XXX Broken
			}

			if (!saOfferFunds.isPositive())
			{
				// Offer is unfunded.
				Log(lsINFO) << "calcOfferFirst: offer unfunded: delete";
			}
			else if (saOfferFunds >= saOfferPays)
			{
				// Offer fully funded.

				// Account transfering funds in to offer always pays inbound fees.

				saOfferIn	= saOfferGets;	// XXX Add in fees?

				saOfferOut	= saOfferPays;

				bDone		= true;
			}
			else
			{
				// Offer partially funded.

				// saOfferIn/saOfferFunds = saOfferGets/saOfferPays
				// XXX Round such that all saOffer funds are exhausted.
				saOfferIn	= (saOfferFunds*saOfferGets)/saOfferPays; // XXX Add in fees?
				saOfferOut	= saOfferFunds;

				bDone		= true;
			}
		}

		if (!bDone)
		{
			// musUnfundedFound.insert(uOfferIndex);
		}
	}
	while (bNext);
}
#endif

#if 0
// If either currency is not stamps, then also calculates vs stamp bridge.
// --> saWanted: Limit of how much is wanted out.
// <-- saPay: How much to pay into the offer.
// <-- saGot: How much to the offer pays out.  Never more than saWanted.
// Given two value's enforce a minimum:
// - reverse: prv is maximum to pay in (including fee) - cur is what is wanted: generally, minimizing prv
// - forward: prv is actual amount to pay in (including fee) - cur is what is wanted: generally, minimizing cur
// Value in is may be rippled or credited from limbo. Value out is put in limbo.
// If next is an offer, the amount needed is in cur reedem.
// XXX What about account mentioned multiple times via offers?
void TransactionEngine::calcNodeOffer(
	bool			bForward,
	bool			bMultiQuality,	// True, if this is the only active path: we can do multiple qualities in this pass.
	const uint160&	uPrvAccountID,	// If 0, then funds from previous offer's limbo
	const uint160&	uPrvCurrencyID,
	const uint160&	uPrvIssuerID,
	const uint160&	uCurCurrencyID,
	const uint160&	uCurIssuerID,

	const STAmount& uPrvRedeemReq,	// --> In limit.
	STAmount&		uPrvRedeemAct,	// <-> In limit achived.
	const STAmount& uCurRedeemReq,	// --> Out limit. Driver when uCurIssuerID == uNxtIssuerID (offer would redeem to next)
	STAmount&		uCurRedeemAct,	// <-> Out limit achived.

	const STAmount& uCurIssueReq,	// --> In limit.
	STAmount&		uCurIssueAct,	// <-> In limit achived.
	const STAmount& uCurIssueReq,	// --> Out limit. Driver when uCurIssueReq != uNxtIssuerID (offer would effectively issue or transfer to next)
	STAmount&		uCurIssueAct,	// <-> Out limit achived.

	STAmount& saPay,
	STAmount& saGot
	) const
{
	TER	terResult	= temUNKNOWN;

	// Direct: not bridging via XNS
	bool			bDirectNext	= true;		// True, if need to load.
	uint256			uDirectQuality;
	uint256			uDirectTip	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);
	uint256			uDirectEnd	= Ledger::getQualityNext(uDirectTip);

	// Bridging: bridging via XNS
	bool			bBridge		= true;		// True, if bridging active. False, missing an offer.
	uint256			uBridgeQuality;
	STAmount		saBridgeIn;				// Amount available.
	STAmount		saBridgeOut;

	bool			bInNext		= true;		// True, if need to load.
	STAmount		saInIn;					// Amount available. Consumed in loop. Limited by offer funding.
	STAmount		saInOut;
	uint256			uInTip;					// Current entry.
	uint256			uInEnd;
	unsigned int	uInEntry;

	bool			bOutNext	= true;
	STAmount		saOutIn;
	STAmount		saOutOut;
	uint256			uOutTip;
	uint256			uOutEnd;
	unsigned int	uOutEntry;

	saPay.zero();
	saPay.setCurrency(uPrvCurrencyID);
	saPay.setIssuer(uPrvIssuerID);

	saNeed	= saWanted;

	if (!uCurCurrencyID && !uPrvCurrencyID)
	{
		// Bridging: Neither currency is XNS.
		uInTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, CURRENCY_XNS, ACCOUNT_XNS);
		uInEnd		= Ledger::getQualityNext(uInTip);
		uOutTip		= Ledger::getBookBase(CURRENCY_XNS, ACCOUNT_XNS, uCurCurrencyID, uCurIssuerID);
		uOutEnd		= Ledger::getQualityNext(uInTip);
	}

	// Find our head offer.

	bool		bRedeeming		= false;
	bool		bIssuing		= false;

	// The price varies as we change between issuing and transfering, so unless bMultiQuality, we must stick with a mode once it
	// is determined.

	if (bBridge && (bInNext || bOutNext))
	{
		// Bridging and need to calculate next bridge rate.
		// A bridge can consist of multiple offers. As offer's are consumed, the effective rate changes.

		if (bInNext)
		{
//					sleInDir	= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uInIndex, uInEnd));
			// Get the next funded offer.
			offerBridgeNext(uInIndex, uInEnd, uInEntry, saInIn, saInOut);	// Get offer limited by funding.
			bInNext		= false;
		}

		if (bOutNext)
		{
//					sleOutDir	= entryCache(ltDIR_NODE, mLedger->getNextLedgerIndex(uOutIndex, uOutEnd));
			offerNext(uOutIndex, uOutEnd, uOutEntry, saOutIn, saOutOut);
			bOutNext	= false;
		}

		if (!uInIndex || !uOutIndex)
		{
			bBridge	= false;	// No more offers to bridge.
		}
		else
		{
			// Have bridge in and out entries.
			// Calculate bridge rate.  Out offer pay ripple fee.  In offer fee is added to in cost.

			saBridgeOut.zero();

			if (saInOut < saOutIn)
			{
				// Limit by in.

				// XXX Need to include fees in saBridgeIn.
				saBridgeIn	= saInIn;	// All of in
				// Limit bridge out: saInOut/saBridgeOut = saOutIn/saOutOut
				// Round such that we would take all of in offer, otherwise would have leftovers.
				saBridgeOut	= (saInOut * saOutOut) / saOutIn;
			}
			else if (saInOut > saOutIn)
			{
				// Limit by out, if at all.

				// XXX Need to include fees in saBridgeIn.
				// Limit bridge in:saInIn/saInOuts = aBridgeIn/saOutIn
				// Round such that would take all of out offer.
				saBridgeIn	= (saInIn * saOutIn) / saInOuts;
				saBridgeOut	= saOutOut;		// All of out.
			}
			else
			{
				// Entries match,

				// XXX Need to include fees in saBridgeIn.
				saBridgeIn	= saInIn;	// All of in
				saBridgeOut	= saOutOut;	// All of out.
			}

			uBridgeQuality	= STAmount::getRate(saBridgeIn, saBridgeOut);	// Inclusive of fees.
		}
	}

	if (bBridge)
	{
		bUseBridge	= !uDirectTip || (uBridgeQuality < uDirectQuality)
	}
	else if (!!uDirectTip)
	{
		bUseBridge	= false
	}
	else
	{
		// No more offers. Declare success, even if none returned.
		saGot		= saWanted-saNeed;
		terResult	= tesSUCCESS;
	}

	if (tesSUCCESS != terResult)
	{
		STAmount&	saAvailIn	= bUseBridge ? saBridgeIn : saDirectIn;
		STAmount&	saAvailOut	= bUseBridge ? saBridgeOut : saDirectOut;

		if (saAvailOut > saNeed)
		{
			// Consume part of offer. Done.

			saNeed	= 0;
			saPay	+= (saNeed*saAvailIn)/saAvailOut; // Round up, prefer to pay more.
		}
		else
		{
			// Consume entire offer.

			saNeed	-= saAvailOut;
			saPay	+= saAvailIn;

			if (bUseBridge)
			{
				// Consume bridge out.
				if (saOutOut == saAvailOut)
				{
					// Consume all.
					saOutOut	= 0;
					saOutIn		= 0;
					bOutNext	= true;
				}
				else
				{
					// Consume portion of bridge out, must be consuming all of bridge in.
					// saOutIn/saOutOut = saSpent/saAvailOut
					// Round?
					saOutIn		-= (saOutIn*saAvailOut)/saOutOut;
					saOutOut	-= saAvailOut;
				}

				// Consume bridge in.
				if (saOutIn == saAvailIn)
				{
					// Consume all.
					saInOut		= 0;
					saInIn		= 0;
					bInNext		= true;
				}
				else
				{
					// Consume portion of bridge in, must be consuming all of bridge out.
					// saInIn/saInOut = saAvailIn/saPay
					// Round?
					saInOut	-= (saInOut*saAvailIn)/saInIn;
					saInIn	-= saAvailIn;
				}
			}
			else
			{
				bDirectNext	= true;
			}
		}
	}
}
#endif


TER	TransactionEngine::doContractAdd(const SerializedTransaction& txn)
{
	Log(lsWARNING) << "doContractAdd> " << txn.getJson(0);

	const uint32 expiration		= txn.getITFieldU32(sfExpiration);
	const uint32 bondAmount		= txn.getITFieldU32(sfBondAmount);
	const uint32 stampEscrow	= txn.getITFieldU32(sfStampEscrow);
	STAmount rippleEscrow		= txn.getITFieldAmount(sfRippleEscrow);
	std::vector<unsigned char>	createCode		= txn.getITFieldVL(sfCreateCode);
	std::vector<unsigned char>	fundCode		= txn.getITFieldVL(sfFundCode);
	std::vector<unsigned char>	removeCode		= txn.getITFieldVL(sfRemoveCode);
	std::vector<unsigned char>	expireCode		= txn.getITFieldVL(sfExpireCode);

	// make sure
	// expiration hasn't passed
	// bond amount is enough
	// they have the stamps for the bond

	// place contract in ledger
	// run create code
	

	if (mLedger->getParentCloseTimeNC() >= expiration)
	{
		Log(lsWARNING) << "doContractAdd: Expired transaction: offer expired";
		return(tefALREADY);
	}
	//TODO: check bond
	//if( txn.getSourceAccount() )

	Contract contract;
	Script::Interpreter interpreter;
	TER	terResult=interpreter.interpret(&contract,txn,createCode);
	if(tesSUCCESS != terResult)
	{

	}

	return(terResult);
}

TER	TransactionEngine::doContractRemove(const SerializedTransaction& txn)
{
	// TODO:
	return(tesSUCCESS);
}

// vim:ts=4
