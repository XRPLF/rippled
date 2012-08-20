//
// XXX Should make sure all fields and are recognized on a transactions.
// XXX Make sure fee is claimed for failed transactions.
// XXX Might uses an unordered set for vector.
//

#include "TransactionEngine.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <queue>

#include "../json/writer.h"

#include "Config.h"
#include "Log.h"
#include "TransactionFormats.h"
#include "utils.h"

// Small for testing, should likely be 32 or 64.
#define DIR_NODE_MAX		2
#define RIPPLE_PATHS_MAX	3

static STAmount saZero(CURRENCY_ONE, 0, 0);
static STAmount saOne(CURRENCY_ONE, 1, 0);

bool transResultInfo(TransactionEngineResult terCode, std::string& strToken, std::string& strHuman)
{
	static struct {
		TransactionEngineResult	terCode;
		const char*				cpToken;
		const char*				cpHuman;
	} transResultInfoA[] = {
		{	tenBAD_ADD_AUTH,		"tenBAD_ADD_AUTH",			"Not authorized to add account."					},
		{	tenBAD_AMOUNT,			"tenBAD_AMOUNT",			"Can only send positive amounts."					},
		{	tenBAD_CLAIM_ID,		"tenBAD_CLAIM_ID",			"Malformed."										},
		{	tenBAD_EXPIRATION,		"tenBAD_EXPIRATION",		"Malformed."										},
		{	tenBAD_GEN_AUTH,		"tenBAD_GEN_AUTH",			"Not authorized to claim generator."				},
		{	tenBAD_ISSUER,			"tenBAD_ISSUER",			"Malformed."										},
		{	tenBAD_OFFER,			"tenBAD_OFFER",				"Malformed."										},
		{	tenBAD_PATH,			"tenBAD_PATH",				"Malformed: path."									},
		{	tenBAD_PATH_COUNT,		"tenBAD_PATH_COUNT",		"Malformed: too many paths."						},
		{	tenBAD_PUBLISH,			"tenBAD_PUBLISH",			"Malformed: bad publish."							},
		{	tenBAD_RIPPLE,			"tenBAD_RIPPLE",			"Ledger prevents ripple from succeeding."			},
		{	tenBAD_SET_ID,			"tenBAD_SET_ID",			"Malformed."										},
		{	tenCLAIMED,				"tenCLAIMED",				"Can not claim a previously claimed account."		},
		{	tenCREATED,				"tenCREATED",				"Can't add an already created account."				},
		{	tenCREATEXNS,			"tenCREATEXNS",				"Can not specify non XNS for Create."				},
		{	tenDST_IS_SRC,			"tenDST_IS_SRC",			"Destination may not be source."					},
		{	tenDST_NEEDED,			"tenDST_NEEDED",			"Destination not specified."						},
		{	tenEXPIRED,				"tenEXPIRED",				"Won't add an expired offer."						},
		{	tenEXPLICITXNS,			"tenEXPLICITXNS",			"XNS is used by default, don't specify it."			},
		{	tenFAILED,				"tenFAILED",				"Something broke horribly"							},
		{	tenGEN_IN_USE,			"tenGEN_IN_USE",			"Generator already in use."							},
		{	tenINSUF_FEE_P,			"tenINSUF_FEE_P",			"fee totally insufficient"							},
		{	tenINVALID,				"tenINVALID",				"The transaction is ill-formed"						},
		{	tenMSG_SET,				"tenMSG_SET",				"Can't change a message key."						},
		{	tenREDUNDANT,			"tenREDUNDANT",				"Sends same currency to self."						},
		{	tenRIPPLE_EMPTY,		"tenRIPPLE_EMPTY",			"PathSet with no paths."							},
		{	tenUNKNOWN,				"tenUNKNOWN",				"The transactions requires logic not implemented yet"	},
		{	terALREADY,				"terALREADY",				"The exact transaction was already in this ledger"	},
		{	terBAD_AUTH,			"terBAD_AUTH",				"Transaction's public key is not authorized."		},
		{	terBAD_AUTH_MASTER,		"terBAD_AUTH_MASTER",		"Auth for unclaimed account needs correct master key."	},
		{	terBAD_LEDGER,			"terBAD_LEDGER",			"Ledger in unexpected state."						},
		{	terBAD_RIPPLE,			"terBAD_RIPPLE",			"No ripple path can be satisfied."					},
		{	terBAD_SEQ,				"terBAD_SEQ",				"This sequence number should be zero for prepaid transactions."	},
		{	terDIR_FULL,			"terDIR_FULL",				"Can not add entry to full dir."					},
		{	terFUNDS_SPENT,			"terFUNDS_SPENT",			"Can't set password, password set funds already spent."	},
		{	terINSUF_FEE_B,			"terINSUF_FEE_B",			"Account balance can't pay fee"						},
		{	terINSUF_FEE_T,			"terINSUF_FEE_T",			"fee insufficient now (account doesn't exist, network load)"	},
		{	terNODE_NOT_FOUND,		"terNODE_NOT_FOUND",		"Can not delete a directory node."					},
		{	terNODE_NOT_MENTIONED,  "terNODE_NOT_MENTIONED",	"Could not remove node from a directory."			},
		{	terNODE_NO_ROOT,        "terNODE_NO_ROOT",			"Directory doesn't exist."							},
		{	terNO_ACCOUNT,			"terNO_ACCOUNT",			"The source account does not exist"					},
		{	terNO_DST,				"terNO_DST",				"The destination does not exist"					},
		{	terNO_LINE_NO_ZERO,		"terNO_LINE_NO_ZERO",		"Can't zero non-existant line, destination might make it."	},
		{	terNO_PATH,				"terNO_PATH",				"No path existed or met transaction/balance requirements"	},
		{	terOFFER_NOT_FOUND,		"terOFFER_NOT_FOUND",		"Can not cancel offer."								},
		{	terOVER_LIMIT,			"terOVER_LIMIT",			"Over limit."										},
		{	terPAST_LEDGER,			"terPAST_LEDGER",			"The transaction expired and can't be applied"		},
		{	terPAST_SEQ,			"terPAST_SEQ",				"This sequence number has already past"				},
		{	terPATH_EMPTY,			"terPATH_EMPTY",			"Path could not send partial amount."				},
		{	terPATH_PARTIAL,		"terPATH_PARTIAL",			"Path could not send full amount."					},
		{	terPRE_SEQ,				"terPRE_SEQ",				"Missing/inapplicable prior transaction"			},
		{	terSET_MISSING_DST,		"terSET_MISSING_DST",		"Can't set password, destination missing."			},
		{	terSUCCESS,				"terSUCCESS",				"The transaction was applied"						},
		{	terUNCLAIMED,			"terUNCLAIMED",				"Can not use an unclaimed account."					},
		{	terUNFUNDED,			"terUNFUNDED",				"Source account had insufficient balance for transaction."	},
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

STAmount TransactionEngine::rippleBalance(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saBalance;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	if (sleRippleState)
	{
		saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);
		if (uToAccountID < uFromAccountID)
			saBalance.negate();
	}
	else
	{
		Log(lsINFO) << "rippleBalance: No credit line between "
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

STAmount TransactionEngine::rippleLimit(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	STAmount		saLimit;
	SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

	assert(sleRippleState);
	if (sleRippleState)
	{
		saLimit	= sleRippleState->getIValueFieldAmount(uToAccountID < uFromAccountID ? sfLowLimit : sfHighLimit);
	}

	return saLimit;

}

uint32 TransactionEngine::rippleTransferRate(const uint160& uIssuerID)
{
	SLE::pointer	sleAccount	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uIssuerID));

	return sleAccount->getIFieldPresent(sfTransferRate)
		? sleAccount->getIFieldU32(sfTransferRate)
		: QUALITY_ONE;
}

// XXX Might not need this, might store in nodes on calc reverse.
uint32 TransactionEngine::rippleQualityIn(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	uint32		uQualityIn	= QUALITY_ONE;

	if (uToAccountID == uFromAccountID)
	{
		nothing();
	}
	else
	{
		SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

		if (sleRippleState)
		{
			SOE_Field	sfField	= uToAccountID < uFromAccountID ? sfLowQualityIn : sfHighQualityIn;

			uQualityIn	= sleRippleState->getIFieldPresent(sfField)
							? sleRippleState->getIFieldU32(sfField)
							: QUALITY_ONE;
			if (!uQualityIn)
				uQualityIn	= 1;
		}
		else
		{
			assert(false);
		}
	}

	return uQualityIn;
}

uint32 TransactionEngine::rippleQualityOut(const uint160& uToAccountID, const uint160& uFromAccountID, const uint160& uCurrencyID)
{
	uint32		uQualityOut	= QUALITY_ONE;

	if (uToAccountID == uFromAccountID)
	{
		nothing();
	}
	else
	{
		SLE::pointer	sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uToAccountID, uFromAccountID, uCurrencyID));

		if (sleRippleState)
		{
			uQualityOut	= sleRippleState->getIFieldU32(uToAccountID < uFromAccountID ? sfLowQualityOut : sfHighQualityOut);
			if (!uQualityOut)
				uQualityOut	= 1;
		}
		else
		{
			assert(false);
		}
	}

	return uQualityOut;
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

		if (uAccountID < uIssuerID)
			saBalance.negate();		// Put balance in low terms.
	}

	return saBalance;
}

// <-- saAmount: amount of uCurrencyID held by uAccountID. May be negative.
STAmount TransactionEngine::accountHolds(const uint160& uAccountID, const uint160& uCurrencyID, const uint160& uIssuerID)
{
	STAmount	saAmount;

	if (uCurrencyID.isZero())
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
//              If the issuer is the same as uAccountID, result is Default.
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

			saTransitFee	= STAmount::multiply(saAmount, saTransitRate, saAmount.getCurrency());
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

		saActual	= saTransitFee.isZero() ? saAmount : saAmount+saTransitFee;

		saActual.setIssuer(uIssuerID);	// XXX Make sure this done in + above.

		rippleCredit(uIssuerID, uReceiverID, saAmount);
		rippleCredit(uSenderID, uIssuerID, saActual);
	}

	return saActual;
}

STAmount TransactionEngine::accountSend(const uint160& uSenderID, const uint160& uReceiverID, const STAmount& saAmount)
{
	STAmount	saActualCost;

	if (saAmount.isNative())
	{
		SLE::pointer		sleSender	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uSenderID));
		SLE::pointer		sleReceiver	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uReceiverID));

		Log(lsINFO) << str(boost::format("accountSend> %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender->getIValueFieldAmount(sfBalance)).getFullText()
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver->getIValueFieldAmount(sfBalance)).getFullText()
			% saAmount.getFullText());

		sleSender->setIFieldAmount(sfBalance, sleSender->getIValueFieldAmount(sfBalance) - saAmount);
		sleReceiver->setIFieldAmount(sfBalance, sleReceiver->getIValueFieldAmount(sfBalance) + saAmount);

		Log(lsINFO) << str(boost::format("accountSend< %s (%s) -> %s (%s) : %s")
			% NewcoinAddress::createHumanAccountID(uSenderID)
			% (sleSender->getIValueFieldAmount(sfBalance)).getFullText()
			% NewcoinAddress::createHumanAccountID(uReceiverID)
			% (sleReceiver->getIValueFieldAmount(sfBalance)).getFullText()
			% saAmount.getFullText());

		entryModify(sleSender);
		entryModify(sleReceiver);

		saActualCost	= saAmount;
	}
	else
	{
		saActualCost	= rippleSend(uSenderID, uReceiverID, saAmount);
	}

	return saActualCost;
}

TransactionEngineResult TransactionEngine::offerDelete(const SLE::pointer& sleOffer, const uint256& uOfferIndex, const uint160& uOwnerID)
{
	uint64					uOwnerNode	= sleOffer->getIFieldU64(sfOwnerNode);
	TransactionEngineResult	terResult	= dirDelete(false, uOwnerNode, Ledger::getOwnerDirIndex(uOwnerID), uOfferIndex);

	if (terSUCCESS == terResult)
	{
		uint256		uDirectory	= sleOffer->getIFieldH256(sfBookDirectory);
		uint64		uBookNode	= sleOffer->getIFieldU64(sfBookNode);

		terResult	= dirDelete(false, uBookNode, uDirectory, uOfferIndex);
	}

	entryDelete(sleOffer);

	return terResult;
}

// <--     uNodeDir: For deletion, present to make dirDelete efficient.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// We only append. This allow for things that watch append only structure to just monitor from the last node on ward.
// Within a node with no deletions order of elements is sequential.  Otherwise, order of elements is random.
TransactionEngineResult TransactionEngine::dirAdd(
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

	return terSUCCESS;
}

// -->    bKeepRoot: True, if we never completely clean up, after we overflow the root node.
// -->     uNodeDir: Node containing entry.
// -->   uRootIndex: The index of the base of the directory.  Nodes are based off of this.
// --> uLedgerIndex: Value to add to directory.
// Ledger must be in a state for this to work.
TransactionEngineResult TransactionEngine::dirDelete(
	bool							bKeepRoot,
	const uint64&					uNodeDir,
	const uint256&					uRootIndex,
	const uint256&					uLedgerIndex)
{
	uint64				uNodeCur	= uNodeDir;
	SLE::pointer		sleNode		= entryCache(ltDIR_NODE, uNodeCur ? Ledger::getDirNodeIndex(uRootIndex, uNodeCur) : uRootIndex);

	assert(sleNode);

	if (!sleNode)
	{
		Log(lsWARNING) << "dirDelete: no such node";

		return terBAD_LEDGER;
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

		return terBAD_LEDGER;
	}

	// Remove the element.
	if (vuiIndexes.size() > 1)
		*it = vuiIndexes[vuiIndexes.size()-1];

	vuiIndexes.resize(vuiIndexes.size()-1);

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

				return terBAD_LEDGER;
			}

			if (!sleNext)
			{
				Log(lsWARNING) << "dirDelete: next node is missing";

				return terBAD_LEDGER;
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

	return terSUCCESS;
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

	return true;
}

// Set the authorized public key for an account.  May also set the generator map.
TransactionEngineResult	TransactionEngine::setAuthorized(const SerializedTransaction& txn, bool bMustSetGenerator)
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

		return tenBAD_GEN_AUTH;
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

		return tenGEN_IN_USE;
	}

	// Set the public key needed to use the account.
	uint160				uAuthKeyID		= bMustSetGenerator
											? hGeneratorID								// Claim
											: txn.getITFieldAccount(sfAuthorizedKey);	// PasswordSet

	mTxnAccount->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	return terSUCCESS;
}

SLE::pointer TransactionEngine::entryCache(LedgerEntryType letType, const uint256& uIndex)
{
	SLE::pointer				sleEntry;

	if (!uIndex.isZero())
	{
		LedgerEntryAction action;
		sleEntry = mNodes.getEntry(uIndex, action);
		if (!sleEntry)
		{
			sleEntry = mLedger->getSLE(uIndex);
			if (sleEntry)
				mNodes.entryCache(sleEntry);
		}
		else if (action == taaDELETE)
			assert(false);
	}

	return sleEntry;
}

SLE::pointer TransactionEngine::entryCreate(LedgerEntryType letType, const uint256& uIndex)
{
	assert(!uIndex.isZero());

	SLE::pointer	sleNew	= boost::make_shared<SerializedLedgerEntry>(letType);
	sleNew->setIndex(uIndex);
	mNodes.entryCreate(sleNew);

	return sleNew;
}

void TransactionEngine::entryDelete(SLE::pointer sleEntry, bool unfunded)
{
	mNodes.entryDelete(sleEntry, unfunded);
}

void TransactionEngine::entryModify(SLE::pointer sleEntry)
{
	mNodes.entryModify(sleEntry);
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

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params)
{
	Log(lsTRACE) << "applyTransaction>";
	assert(mLedger);
	mLedgerParentCloseTime	= mLedger->getParentCloseTimeNC();
	mNodes.init(txn.getTransactionID(), mLedger->getLedgerSeq());

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

	TransactionEngineResult terResult = terSUCCESS;

	uint256 txID = txn.getTransactionID();
	if (!txID)
	{
		Log(lsWARNING) << "applyTransaction: invalid transaction id";

		terResult	= tenINVALID;
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

	if (terSUCCESS == terResult)
		naSigningPubKey	= NewcoinAddress::createAccountPublic(txn.peekSigningPubKey());

	// Consistency: really signed.
	if ((terSUCCESS == terResult) && ((params & tepNO_CHECK_SIGN) == 0) && !txn.checkSign(naSigningPubKey))
	{
		Log(lsWARNING) << "applyTransaction: Invalid transaction: bad signature";

		terResult	= tenINVALID;
	}

	STAmount	saCost		= theConfig.FEE_DEFAULT;

	// Customize behavoir based on transaction type.
	if (terSUCCESS == terResult)
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
			case ttINVOICE:
			case ttOFFER_CREATE:
			case ttOFFER_CANCEL:
			case ttPASSWORD_FUND:
			case ttWALLET_ADD:
				nothing();
				break;

			case ttINVALID:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: ttINVALID transaction type";
				terResult = tenINVALID;
				break;

			default:
				Log(lsWARNING) << "applyTransaction: Invalid transaction: unknown transaction type";
				terResult = tenUNKNOWN;
				break;
		}
	}

	STAmount saPaid = txn.getTransactionFee();

	if (terSUCCESS == terResult && (params & tepNO_CHECK_FEE) == tepNONE)
	{
		if (!saCost.isZero())
		{
			if (saPaid < saCost)
			{
				Log(lsINFO) << "applyTransaction: insufficient fee";

				terResult	= tenINSUF_FEE_P;
			}
		}
		else
		{
			if (!saPaid.isZero())
			{
				// Transaction is malformed.
				Log(lsWARNING) << "applyTransaction: fee not allowed";

				terResult	= tenINSUF_FEE_P;
			}
		}
	}

	// Get source account ID.
	mTxnAccountID	= txn.getSourceAccount().getAccountID();
	if (terSUCCESS == terResult && !mTxnAccountID)
	{
		Log(lsWARNING) << "applyTransaction: bad source id";

		terResult	= tenINVALID;
	}

	if (terSUCCESS != terResult)
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
		Log(lsTRACE) << str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			txn.getSourceAccount().humanAccountID());

		terResult			= terNO_ACCOUNT;
	}
	else
	{
		saSrcBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);
		bHaveAuthKey	= mTxnAccount->getIFieldPresent(sfAuthorizedKey);
	}

	// Check if account claimed.
	if (terSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				if (bHaveAuthKey)
				{
					Log(lsWARNING) << "applyTransaction: Account already claimed.";

					terResult	= tenCLAIMED;
				}
				break;

			default:
				nothing();
				break;
		}
	}

	// Consistency: Check signature
	if (terSUCCESS == terResult)
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

					terResult	= tenBAD_CLAIM_ID;
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

					terResult	= tenBAD_SET_ID;
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

					terResult	= terBAD_AUTH;
				}
				else
				{
					Log(lsINFO) << "applyTransaction: Invalid: Not authorized to use account.";

					terResult	= terBAD_AUTH_MASTER;
				}
				break;
		}
	}

	// Deduct the fee, so it's not available during the transaction.
	// Will only write the account back, if the transaction succeeds.
	if (terSUCCESS != terResult || saCost.isZero())
	{
		nothing();
	}
	else if (saSrcBalance < saPaid)
	{
		Log(lsINFO)
			<< str(boost::format("applyTransaction: Delay: insufficent balance: balance=%s paid=%s")
				% saSrcBalance.getText()
				% saPaid.getText());

		terResult	= terINSUF_FEE_B;
	}
	else
	{
		mTxnAccount->setIFieldAmount(sfBalance, saSrcBalance - saPaid);
	}

	// Validate sequence
	if (terSUCCESS != terResult)
	{
		nothing();
	}
	else if (!saCost.isZero())
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

				terResult	= terALREADY;
			}
			else
			{
				Log(lsWARNING) << "applyTransaction: past sequence number";

				terResult	= terPAST_SEQ;
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

			terResult	= terPAST_SEQ;
		}
	}
	mTxnAccount->setIFieldU32(sfLastSignedSeq, mLedger->getLedgerSeq());

	if (terSUCCESS == terResult)
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
				terResult = tenINVALID;
				break;

			case ttINVOICE:
				terResult = doInvoice(txn);
				break;

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

			default:
				terResult = tenUNKNOWN;
				break;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	transResultInfo(terResult, strToken, strHuman);

	Log(lsINFO) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

	if (terSUCCESS == terResult)
	{
		txnWrite();

		Serializer s;

		txn.add(s);

		// XXX add failed status too
		// XXX do fees as need.
		if (!mLedger->addTransaction(txID, s))
			assert(false);

		if ((params & tepUPDATE_TOTAL) != tepNONE)
			mLedger->destroyCoins(saPaid.getNValue());
	}

	mTxnAccount	= SLE::pointer();
	mNodes.clear();
	mUnfunded.clear();

	return terResult;
}

TransactionEngineResult TransactionEngine::doAccountSet(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doAccountSet>";

	//
	// EmailHash
	//

	if (txn.getITFieldPresent(sfEmailHash))
	{
		uint128		uHash	= txn.getITFieldH128(sfEmailHash);

		if (uHash.isZero())
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

		if (uHash.isZero())
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
	else if (mTxnAccount->getIFieldPresent(sfMessageKey))
	{
		Log(lsINFO) << "doAccountSet: can not change message key";

		return tenMSG_SET;
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

		return tenBAD_PUBLISH;
	}
	else if (bPublishHash && bPublishSize)
	{
		uint256		uHash	= txn.getITFieldH256(sfPublishHash);
		uint32		uSize	= txn.getITFieldU32(sfPublishSize);

		if (uHash.isZero())
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

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doClaim(const SerializedTransaction& txn)
{
	Log(lsINFO) << "doClaim>";

	TransactionEngineResult	terResult	= setAuthorized(txn, true);

	Log(lsINFO) << "doClaim<";

	return terResult;
}

TransactionEngineResult TransactionEngine::doCreditSet(const SerializedTransaction& txn)
{
	TransactionEngineResult	terResult	= terSUCCESS;
	Log(lsINFO) << "doCreditSet>";

	// Check if destination makes sense.
	uint160				uDstAccountID	= txn.getITFieldAccount(sfDestination);

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Destination account not specifed.";

		return tenDST_NEEDED;
	}
	else if (mTxnAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Can not extend credit to self.";

		return tenDST_IS_SRC;
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
		if (saLimitAmount.isZero())
		{
			// Zeroing line.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bLow			= uLowID == uSrcAccountID;
			bool		bHigh			= uLowID == uDstAccountID;
			bool		bBalanceZero	= sleRippleState->getIValueFieldAmount(sfBalance).isZero();
			STAmount	saDstLimit		= sleRippleState->getIValueFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);
			bool		bDstLimitZero	= saDstLimit.isZero();

			assert(bLow || bHigh);

			if (bBalanceZero && bDstLimitZero)
			{
				// Zero balance and eliminating last limit.

				bDelIndex	= true;
				terResult	= dirDelete(false, uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex());
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
	else if (saLimitAmount.isZero())
	{
		Log(lsINFO) << "doCreditSet: Redundant: Setting non-existant ripple line to 0.";

		return terNO_LINE_NO_ZERO;
	}
	else
	{
		// Create a new ripple line.
		sleRippleState	= entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uCurrencyID));

		Log(lsINFO) << "doCreditSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setIFieldAmount(sfBalance, STAmount(uCurrencyID));	// Zero balance in currency.
		sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, saLimitAmount);
		sleRippleState->setIFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, STAmount(uCurrencyID));
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, mTxnAccountID);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uDstAccountID);
		if (uQualityIn)
			sleRippleState->setIFieldU32(bFlipped ? sfHighQualityIn : sfLowQualityIn, uQualityIn);
		if (uQualityOut)
			sleRippleState->setIFieldU32(bFlipped ? sfHighQualityOut : sfLowQualityOut, uQualityOut);

		uint64			uSrcRef;							// Ignored, dirs never delete.

		terResult	= dirAdd(uSrcRef, Ledger::getOwnerDirIndex(mTxnAccountID), sleRippleState->getIndex());

		if (terSUCCESS == terResult)
			terResult	= dirAdd(uSrcRef, Ledger::getOwnerDirIndex(uDstAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doCreditSet<";

	return terResult;
}

TransactionEngineResult TransactionEngine::doNicknameSet(const SerializedTransaction& txn)
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

		if (bMinOffer && !!saMinOffer)
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

		if (bMinOffer && !!saMinOffer)
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
	}

	std::cerr << "doNicknameSet<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordFund(const SerializedTransaction& txn)
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

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordSet(const SerializedTransaction& txn)
{
	std::cerr << "doPasswordSet>" << std::endl;

	if (mTxnAccount->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doPasswordSet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	mTxnAccount->setFlag(lsfPasswordSpent);

	TransactionEngineResult	terResult	= setAuthorized(txn, false);

	std::cerr << "doPasswordSet<" << std::endl;

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
// <-- terResult : terSUCCESS = no error and if !bAllowPartial complelely satisfied wanted.
// <-> usOffersDeleteAlways:
// <-> usOffersDeleteOnSuccess:
TransactionEngineResult calcOfferFill(paymentNode& pnSrc, paymentNode& pnDst, bool bAllowPartial)
{
	TransactionEngineResult	terResult;

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

		if (terSUCCESS == terResult)
		{
			// Issue to wanted.
			terResult	= calcOfferFill(
				pnDst.saWanted,		// As much as wanted is available, limited by credit limit.
				pnSrc.saIOUIssue,
				pnDst.saIOUAccept,
				bAllowPartial);
		}

		if (terSUCCESS == terResult && !bAllowPartial)
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

		if (sleOffer->getIFieldPresent(sfGetsIssuer))
			saOfferPays.setIssuer(sleOffer->getIValueFieldAccount(sfGetsIssuer).getAccountID());

		if (sleOffer->getIFieldPresent(sfPaysIssuer))
			saOfferGets.setIssuer(sleOffer->getIValueFieldAccount(sfPaysIssuer).getAccountID());

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
			// mUnfunded.insert(uOfferIndex);
		}
	}
	while (bNext);
}
#endif

// <-- bSuccess: false= no transfer
// XXX Make sure missing ripple path is addressed cleanly.
bool TransactionEngine::calcNodeOfferRev(
	unsigned int		uIndex,				// 0 < uIndex < uLast-1
	PathState::pointer	pspCur,
	bool				bMultiQuality
	)
{
	bool			bSuccess		= false;

	paymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	paymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	paymentNode&	pnNxt			= pspCur->vpnNodes[uIndex+1];

	const uint160&	uPrvCurrencyID	= pnPrv.uCurrencyID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const uint160&	uCurCurrencyID	= pnCur.uCurrencyID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uNxtCurrencyID	= pnNxt.uCurrencyID;
	const uint160&	uNxtIssuerID	= pnNxt.uIssuerID;
	const uint160&	uNxtAccountID	= pnNxt.uAccountID;

	const STAmount	saTransferRate	= STAmount(CURRENCY_ONE, rippleTransferRate(uCurIssuerID), -9);

	uint256			uDirectTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
	const uint256	uDirectEnd		= Ledger::getQualityNext(uDirectTip);
	bool			bAdvance		= !entryCache(ltDIR_NODE, uDirectTip);

	STAmount&		saPrvDlvReq		= pnPrv.saRevDeliver;	// To be adjusted.
	STAmount		saPrvDlvAct;

	const STAmount&	saCurDlvReq		= pnCur.saRevDeliver;	// Reverse driver.
	STAmount		saCurDlvAct;

	while (!!uDirectTip	// Have a quality.
		&& saCurDlvAct != saCurDlvReq)
	{
		// Get next quality.
		if (bAdvance)
		{
			uDirectTip		= mLedger->getNextLedgerIndex(uDirectTip, uDirectEnd);
		}
		else
		{
			bAdvance		= true;
		}

		if (!!uDirectTip)
		{
			// Do a directory.
			// - Drive on computing saCurDlvAct to derive saPrvDlvAct.
			// XXX Behave well, if entry type is wrong (someone beat us to using the hash)
			SLE::pointer	sleDirectDir	= entryCache(ltDIR_NODE, uDirectTip);
			STAmount		saOfrRate		= STAmount::setRate(Ledger::getQuality(uDirectTip), uCurCurrencyID);	// For correct ratio
			unsigned int	uEntry			= 0;
			uint256			uCurIndex;

			while (saCurDlvReq != saCurDlvAct	// Have not met request.
				&& dirNext(uDirectTip, sleDirectDir, uEntry, uCurIndex))
			{
				SLE::pointer	sleCurOfr			= entryCache(ltOFFER, uCurIndex);
				uint160			uCurOfrAccountID	= sleCurOfr->getIValueFieldAccount(sfAccount).getAccountID();
				STAmount		saCurOfrOutReq		= sleCurOfr->getIValueFieldAmount(sfTakerGets);
				STAmount		saCurOfrIn			= sleCurOfr->getIValueFieldAmount(sfTakerPays);
					// XXX Move issuer into STAmount
					if (sleCurOfr->getIFieldPresent(sfGetsIssuer))
						saCurOfrOutReq.setIssuer(sleCurOfr->getIValueFieldAccount(sfGetsIssuer).getAccountID());

					if (sleCurOfr->getIFieldPresent(sfPaysIssuer))
						saCurOfrIn.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());

				STAmount		saCurOfrFunds		= accountFunds(uCurOfrAccountID, saCurOfrOutReq);	// Funds left.

				// XXX Offer is also unfunded if funding source previously mentioned.
				if (!saCurOfrFunds)
				{
					// Offer is unfunded.
					Log(lsINFO) << "calcNodeOfferRev: encountered unfunded offer";

					// XXX Mark unfunded.
				}
				else if (sleCurOfr->getIFieldPresent(sfExpiration) && sleCurOfr->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
				{
					// Offer is expired.
					Log(lsINFO) << "calcNodeOfferRev: encountered expired offer";

					// XXX Mark unfunded.
				}
				else if (!!uNxtAccountID)
				{
					// Next is an account.

					STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtAccountID == uCurIssuerID
												? saOne
												: saTransferRate;
					bool		bFee		= saFeeRate != saOne;

					STAmount	saOutBase	= MIN(saCurOfrOutReq, saCurDlvReq-saCurDlvAct);	// Limit offer out by needed.
					STAmount	saOutCost	= MIN(
												bFee
													? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
													: saOutBase,
												saCurOfrFunds);								// Limit cost by fees & funds.
					STAmount	saOutDlvAct	= bFee
													? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
													: saOutCost;							// Out amount after fees.
					STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uPrvCurrencyID);	// Compute input w/o fees required.

					saCurDlvAct				+= saOutDlvAct;									// Portion of driver served.
					saPrvDlvAct				+= saInDlvAct;									// Portion needed in previous.
				}
				else
				{
					// Next is an offer.

					uint256			uNxtTip		= Ledger::getBookBase(uCurCurrencyID, uCurIssuerID, uNxtCurrencyID, uNxtIssuerID);
					uint256			uNxtEnd		= Ledger::getQualityNext(uNxtTip);
					bool			bNxtAdvance	= !entryCache(ltDIR_NODE, uNxtTip);

					while (!!uNxtTip					// Have a quality.
						&& saCurDlvAct != saCurDlvReq)	// Have more to do.
					{
						if (bNxtAdvance)
						{
							uNxtTip		= mLedger->getNextLedgerIndex(uNxtTip, uNxtEnd);
						}
						else
						{
							bNxtAdvance		= true;
						}

						if (!!uNxtTip)
						{
							// Do a directory.
							// - Drive on computing saCurDlvAct to derive saPrvDlvAct.
							SLE::pointer	sleNxtDir	= entryCache(ltDIR_NODE, uNxtTip);
// ??? STAmount		saOfrRate		= STAmount::setRate(STAmount::getQuality(uNxtTip), uCurCurrencyID);	// For correct ratio
							unsigned int	uEntry			= 0;
							uint256			uNxtIndex;

							while (saCurDlvReq != saCurDlvAct	// Have not met request.
								&& dirNext(uNxtTip, sleNxtDir, uEntry, uNxtIndex))
							{
								// YYY This could combine offers with the same fee before doing math.
								SLE::pointer	sleNxtOfr			= entryCache(ltOFFER, uNxtIndex);
								uint160			uNxtOfrAccountID	= sleNxtOfr->getIValueFieldAccount(sfAccount).getAccountID();
								STAmount		saNxtOfrIn			= sleNxtOfr->getIValueFieldAmount(sfTakerPays);
									// XXX Move issuer into STAmount
									if (sleNxtOfr->getIFieldPresent(sfPaysIssuer))
										saNxtOfrIn.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());

								STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtOfrAccountID == uCurIssuerID
															? saOne
															: saTransferRate;
								bool		bFee		= saFeeRate != saOne;

								STAmount	saOutBase	= MIN(saCurOfrOutReq, saCurDlvReq-saCurDlvAct);	// Limit offer out by needed.
											saOutBase	= MIN(saOutBase, saNxtOfrIn);					// Limit offer out by supplying offer.
								STAmount	saOutCost	= MIN(
															bFee
																? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
																: saOutBase,
															saCurOfrFunds);								// Limit cost by fees & funds.
								STAmount	saOutDlvAct	= bFee
															? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
															: saOutCost;								// Out amount after fees.
								STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uPrvCurrencyID);	// Compute input w/o fees required.

								saCurDlvAct				+= saOutDlvAct;									// Portion of driver served.
								saPrvDlvAct				+= saInDlvAct;									// Portion needed in previous.
							}
						}

						// Do another nxt directory iff bMultiQuality
						if (!bMultiQuality)
							uNxtTip	= 0;
					}
				}
			}
		}

		// Do another cur directory iff bMultiQuality
		if (!bMultiQuality)
			uDirectTip	= 0;
	}

	if (saPrvDlvAct)
	{
		saPrvDlvReq	= saPrvDlvAct;	// Adjust request.
		bSuccess	= true;
	}

	return bSuccess;
}

bool TransactionEngine::calcNodeOfferFwd(
	unsigned int		uIndex,				// 0 < uIndex < uLast-1
	PathState::pointer	pspCur,
	bool				bMultiQuality
	)
{
	bool			bSuccess		= false;

	paymentNode&	pnPrv			= pspCur->vpnNodes[uIndex-1];
	paymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	paymentNode&	pnNxt			= pspCur->vpnNodes[uIndex+1];

	const uint160&	uPrvCurrencyID	= pnPrv.uCurrencyID;
	const uint160&	uPrvIssuerID	= pnPrv.uIssuerID;
	const uint160&	uCurCurrencyID	= pnCur.uCurrencyID;
	const uint160&	uCurIssuerID	= pnCur.uIssuerID;
	const uint160&	uNxtCurrencyID	= pnNxt.uCurrencyID;
	const uint160&	uNxtIssuerID	= pnNxt.uIssuerID;

	const uint160&	uNxtAccountID	= pnNxt.uAccountID;
	const STAmount	saTransferRate	= STAmount(CURRENCY_ONE, rippleTransferRate(uCurIssuerID), -9);

	uint256			uDirectTip		= Ledger::getBookBase(uPrvCurrencyID, uPrvIssuerID, uCurCurrencyID, uCurIssuerID);
	const uint256	uDirectEnd		= Ledger::getQualityNext(uDirectTip);
	bool			bAdvance		= !entryCache(ltDIR_NODE, uDirectTip);

	const STAmount&	saPrvDlvReq		= pnPrv.saFwdDeliver;	// Forward driver.
	STAmount		saPrvDlvAct;

	STAmount&		saCurDlvReq		= pnCur.saFwdDeliver;
	STAmount		saCurDlvAct;

	while (!!uDirectTip										// Have a quality.
		&& saPrvDlvAct != saPrvDlvReq)
	{
		// Get next quality.
		if (bAdvance)
		{
			uDirectTip		= mLedger->getNextLedgerIndex(uDirectTip, uDirectEnd);
		}
		else
		{
			bAdvance		= true;
		}

		if (!!uDirectTip)
		{
			// Do a directory.
			// - Drive on computing saPrvDlvAct to derive saCurDlvAct.
			SLE::pointer	sleDirectDir	= entryCache(ltDIR_NODE, uDirectTip);
			STAmount		saOfrRate		= STAmount::setRate(Ledger::getQuality(uDirectTip), uCurCurrencyID);	// For correct ratio
			unsigned int	uEntry			= 0;
			uint256			uCurIndex;

			while (saPrvDlvReq != saPrvDlvAct	// Have not met request.
				&& dirNext(uDirectTip, sleDirectDir, uEntry, uCurIndex))
			{
				SLE::pointer	sleCurOfr			= entryCache(ltOFFER, uCurIndex);
				uint160			uCurOfrAccountID	= sleCurOfr->getIValueFieldAccount(sfAccount).getAccountID();
				STAmount		saCurOfrOutReq		= sleCurOfr->getIValueFieldAmount(sfTakerGets);
				STAmount		saCurOfrInReq		= sleCurOfr->getIValueFieldAmount(sfTakerPays);
					// XXX Move issuer into STAmount
					if (sleCurOfr->getIFieldPresent(sfGetsIssuer))
						saCurOfrOutReq.setIssuer(sleCurOfr->getIValueFieldAccount(sfGetsIssuer).getAccountID());

					if (sleCurOfr->getIFieldPresent(sfPaysIssuer))
						saCurOfrInReq.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());
				STAmount		saCurOfrInAct;
				STAmount		saCurOfrFunds		= accountFunds(uCurOfrAccountID, saCurOfrOutReq);	// Funds left.

				saCurOfrInReq	= MIN(saCurOfrInReq, saPrvDlvReq-saPrvDlvAct);

				if (!!uNxtAccountID)
				{
					// Next is an account.

					const STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtAccountID == uCurIssuerID
													? saOne
						 							: saTransferRate;
					const bool		bFee		= saFeeRate != saOne;

					const STAmount	saOutPass	= STAmount::divide(saCurOfrInReq, saOfrRate, uCurCurrencyID);
					const STAmount	saOutBase	= MIN(saCurOfrOutReq, saOutPass);				// Limit offer out by needed.
					const STAmount	saOutCost	= MIN(
													bFee
														? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
														: saOutBase,
													saCurOfrFunds);								// Limit cost by fees & funds.
					const STAmount	saOutDlvAct	= bFee
														? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
														: saOutCost;		// Out amount after fees.
					const STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uCurCurrencyID);	// Compute input w/o fees required.

					saCurDlvAct				+= saOutDlvAct;									// Portion of driver served.
					saPrvDlvAct				+= saInDlvAct;									// Portion needed in previous.
				}
				else
				{
					// Next is an offer.

					uint256			uNxtTip		= Ledger::getBookBase(uCurCurrencyID, uCurIssuerID, uNxtCurrencyID, uNxtIssuerID);
					const uint256	uNxtEnd		= Ledger::getQualityNext(uNxtTip);
					bool			bNxtAdvance	= !entryCache(ltDIR_NODE, uNxtTip);

					while (!!uNxtTip						// Have a quality.
						&& saPrvDlvAct != saPrvDlvReq)	// Have more to do.
					{
						if (bNxtAdvance)
						{
							uNxtTip		= mLedger->getNextLedgerIndex(uNxtTip, uNxtEnd);
						}
						else
						{
							bNxtAdvance		= true;
						}

						if (!!uNxtTip)
						{
							// Do a directory.
							// - Drive on computing saCurDlvAct to derive saPrvDlvAct.
							SLE::pointer	sleNxtDir	= entryCache(ltDIR_NODE, uNxtTip);
// ??? STAmount		saOfrRate		= STAmount::setRate(STAmount::getQuality(uNxtTip));	// For correct ratio
							unsigned int	uEntry			= 0;
							uint256			uNxtIndex;

							while (saPrvDlvReq != saPrvDlvAct	// Have not met request.
								&& dirNext(uNxtTip, sleNxtDir, uEntry, uNxtIndex))
							{
								// YYY This could combine offers with the same fee before doing math.
								SLE::pointer	sleNxtOfr			= entryCache(ltOFFER, uNxtIndex);
								const uint160	uNxtOfrAccountID	= sleNxtOfr->getIValueFieldAccount(sfAccount).getAccountID();
								STAmount		saNxtOfrIn			= sleNxtOfr->getIValueFieldAmount(sfTakerPays);
									// XXX Move issuer into STAmount
									if (sleNxtOfr->getIFieldPresent(sfPaysIssuer))
										saNxtOfrIn.setIssuer(sleCurOfr->getIValueFieldAccount(sfPaysIssuer).getAccountID());

								const STAmount	saFeeRate	= uCurOfrAccountID == uCurIssuerID || uNxtOfrAccountID == uCurIssuerID
																? saOne
																: saTransferRate;
								const bool		bFee		= saFeeRate != saOne;

								const STAmount	saInBase	= saCurOfrInReq-saCurOfrInAct;
								const STAmount	saOutPass	= STAmount::divide(saInBase, saOfrRate, uCurCurrencyID);
								STAmount		saOutBase	= MIN(saCurOfrOutReq, saOutPass);				// Limit offer out by needed.
												saOutBase	= MIN(saOutBase, saNxtOfrIn);					// Limit offer out by supplying offer.
								const STAmount	saOutCost	= MIN(
															bFee
																? STAmount::multiply(saOutBase, saFeeRate, uCurCurrencyID)
																: saOutBase,
															saCurOfrFunds);								// Limit cost by fees & funds.
								const STAmount	saOutDlvAct	= bFee
																? STAmount::divide(saOutCost, saFeeRate, uCurCurrencyID)
																: saOutCost;							// Out amount after fees.
								const STAmount	saInDlvAct	= STAmount::multiply(saOutDlvAct, saOfrRate, uPrvCurrencyID);	// Compute input w/o fees required.

								saCurOfrInAct			+= saOutDlvAct;									// Portion of driver served.
								saPrvDlvAct				+= saOutDlvAct;									// Portion needed in previous.
								saCurDlvAct				+= saInDlvAct;									// Portion of driver served.
							}
						}

						// Do another nxt directory iff bMultiQuality
						if (!bMultiQuality)
							uNxtTip	= 0;
					}
				}
			}
		}

		// Do another cur directory iff bMultiQuality
		if (!bMultiQuality)
			uDirectTip	= 0;
	}

	if (saCurDlvAct)
	{
		saCurDlvReq	= saCurDlvAct;	// Adjust request.
		bSuccess	= true;
	}

	return bSuccess;
}

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
	TransactionEngineResult	terResult	= tenUNKNOWN;

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
		terResult	= terSUCCESS;
	}

	if (terSUCCESS != terResult)
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
	STAmount& saCurAct)			// <-> out limit achieved.
{
	Log(lsINFO) << str(boost::format("calcNodeRipple> uQualityIn=%d uQualityOut=%d saPrvReq=%s saCurReq=%s saPrvAct=%s saCurAct=%s")
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
	Log(lsINFO) << str(boost::format("calcNodeRipple: bPrvUnlimited=%d saPrv=%s saCur=%s")
		% bPrvUnlimited
		% saPrv.getFullText()
		% saCur.getFullText());
#endif

	if (uQualityIn >= uQualityOut)
	{
		// No fee.
		Log(lsINFO) << str(boost::format("calcNodeRipple: No fees"));

		STAmount	saTransfer	= bPrvUnlimited ? saCur : MIN(saPrv, saCur);

		saPrvAct	+= saTransfer;
		saCurAct	+= saTransfer;
	}
	else
	{
		// Fee.
		Log(lsINFO) << str(boost::format("calcNodeRipple: Fee"));

		uint160		uCurrencyID	= saCur.getCurrency();
		STAmount	saCurIn		= STAmount::divide(STAmount::multiply(saCur, uQualityOut, uCurrencyID), uQualityIn, uCurrencyID);

Log(lsINFO) << str(boost::format("calcNodeRipple: bPrvUnlimited=%d saPrv=%s saCurIn=%s") % bPrvUnlimited % saPrv.getFullText() % saCurIn.getFullText());
		if (bPrvUnlimited || saCurIn <= saPrv)
		{
			// All of cur. Some amount of prv.
			saCurAct	+= saCur;
			saPrvAct	+= saCurIn;
Log(lsINFO) << str(boost::format("calcNodeRipple:3c: saCurReq=%s saPrvAct=%s") % saCurReq.getFullText() % saPrvAct.getFullText());
		}
		else
		{
			// A part of cur. All of prv. (cur as driver)
			uint160		uCurrencyID	= saPrv.getCurrency();
			STAmount	saCurOut	= STAmount::divide(STAmount::multiply(saPrv, uQualityIn, uCurrencyID), uQualityOut, uCurrencyID);
Log(lsINFO) << str(boost::format("calcNodeRipple:4: saCurReq=%s") % saCurReq.getFullText());

			saCurAct	+= saCurOut;
			saPrvAct	= saPrvReq;
		}
	}

	Log(lsINFO) << str(boost::format("calcNodeRipple< uQualityIn=%d uQualityOut=%d saPrvReq=%s saCurReq=%s saPrvAct=%s saCurAct=%s")
		% uQualityIn
		% uQualityOut
		% saPrvReq.getFullText()
		% saCurReq.getFullText()
		% saPrvAct.getFullText()
		% saCurAct.getFullText());
}

// Calculate saPrvRedeemReq, saPrvIssueReq, saPrvDeliver;
bool TransactionEngine::calcNodeAccountRev(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality)
{
	bool			bSuccess		= true;
	const unsigned int	uLast		= pspCur->vpnNodes.size() - 1;

	paymentNode&	pnPrv			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	paymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	paymentNode&	pnNxt			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	const bool		bRedeem			= !!(pnCur.uFlags & STPathElement::typeRedeem);
	const bool		bPrvRedeem		= !!(pnPrv.uFlags & STPathElement::typeRedeem);
	const bool		bIssue			= !!(pnCur.uFlags & STPathElement::typeIssue);
	const bool		bPrvIssue		= !!(pnPrv.uFlags & STPathElement::typeIssue);
	const bool		bPrvAccount		= !uIndex || !!(pnPrv.uFlags & STPathElement::typeAccount);
	const bool		bNxtAccount		= uIndex == uLast || !!(pnNxt.uFlags & STPathElement::typeAccount);

	const uint160&	uPrvAccountID	= pnPrv.uAccountID;
	const uint160&	uCurAccountID	= pnCur.uAccountID;
	const uint160&	uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&	uCurrencyID		= pnCur.uCurrencyID;

	const uint32	uQualityIn		= uIndex ? rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID) : 1;
	const uint32	uQualityOut		= uIndex != uLast ? rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID) : 1;

	// For bPrvAccount
	const STAmount	saPrvBalance	= uIndex && bPrvAccount ? rippleBalance(uCurAccountID, uPrvAccountID, uCurrencyID) : STAmount(uCurrencyID);
	const STAmount	saPrvLimit		= uIndex && bPrvAccount ? rippleLimit(uCurAccountID, uPrvAccountID, uCurrencyID) : STAmount(uCurrencyID);

	const STAmount	saPrvRedeemReq	= bPrvRedeem && saPrvBalance.isNegative() ? -saPrvBalance : STAmount(uCurrencyID, 0);
	STAmount&		saPrvRedeemAct	= pnPrv.saRevRedeem;

	const STAmount	saPrvIssueReq	= bPrvIssue ? saPrvLimit - saPrvBalance : STAmount(uCurrencyID);
	STAmount&		saPrvIssueAct	= pnPrv.saRevIssue;

	// For !bPrvAccount
	const STAmount	saPrvDeliverReq	= STAmount(uCurrencyID, -1);	// Unlimited.
	STAmount&		saPrvDeliverAct	= pnPrv.saRevDeliver;

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount		saCurRedeemAct(saCurRedeemReq.getCurrency());

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount		saCurIssueAct(saCurIssueReq.getCurrency());		// Track progress.

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount		saCurDeliverAct(saCurDeliverReq.getCurrency());

	// For uIndex == uLast
	const STAmount&	saCurWantedReq	= pspCur->saOutReq;				// XXX Credit limits?
	// STAmount		saPrvDeliverReq	= saPrvBalance.isPositive() ? saPrvLimit - saPrvBalance : saPrvLimit;
	STAmount		saCurWantedAct(saCurWantedReq.getCurrency());

	Log(lsINFO) << str(boost::format("calcNodeAccountRev> uIndex=%d/%d saPrvRedeemReq=%s/%s saPrvIssueReq=%s/%s saCurWantedReq=%s/%s")
		% uIndex
		% uLast
		% saPrvRedeemReq.getText()
		% saPrvRedeemReq.getHumanCurrency()
		% saPrvIssueReq.getText()
		% saPrvIssueReq.getHumanCurrency()
		% saCurWantedReq.getText()
		% saCurWantedReq.getHumanCurrency());

	Log(lsINFO) << pspCur->getJson();

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
			Log(lsINFO) << str(boost::format("calcNodeAccountRev: account --> ACCOUNT --> $"));

			// Calculate redeem
			if (bRedeem
				&& saPrvRedeemReq)								// Previous has IOUs to redeem.
			{
				// Redeem at 1:1
				Log(lsINFO) << str(boost::format("calcNodeAccountRev: Redeem at 1:1"));

				saCurWantedAct		= MIN(saPrvRedeemReq, saCurWantedReq);
				saPrvRedeemAct		= saCurWantedAct;
			}

			// Calculate issuing.
			if (bIssue
				&& saCurWantedReq != saCurWantedAct				// Need more.
				&& saPrvIssueReq)								// Will accept IOUs.
			{
				// Rate: quality in : 1.0
				Log(lsINFO) << str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0"));

				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurWantedReq, saPrvIssueAct, saCurWantedAct);
			}

			if (!saCurWantedAct)
			{
				// Must have processed something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
		else
		{
			// ^|account --> ACCOUNT --> account

			// redeem (part 1) -> redeem
			if (bPrvRedeem
				&& bRedeem								// Allowed to redeem.
				&& saCurRedeemReq						// Next wants us to redeem.
				&& saPrvBalance.isNegative())			// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				Log(lsINFO) << str(boost::format("calcNodeAccountRev: Rate : 1.0 : quality out"));

				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct);
			}

			// redeem (part 2) -> issue.
			if (bPrvRedeem
				&& bIssue								// Allowed to issue.
				&& saCurRedeemReq != saCurRedeemAct		// Can only if issue if more can not be redeemed.
				&& saPrvBalance.isNegative()			// Previous still has IOUs.
				&& saCurIssueReq)						// Need some issued.
			{
				// Rate : 1.0 : transfer_rate
				Log(lsINFO) << str(boost::format("calcNodeAccountRev: Rate : 1.0 : transfer_rate"));

				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct);
			}

			// issue (part 1)-> redeem
			if (bPrvIssue
				&& bRedeem								// Allowed to redeem.
				&& saCurRedeemReq != saCurRedeemAct		// Can only redeem if more to be redeemed.
				&& !saPrvBalance.isNegative())			// Previous has no IOUs.
			{
				// Rate: quality in : quality out
				Log(lsINFO) << str(boost::format("calcNodeAccountRev: Rate: quality in : quality out"));

				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct);
			}

			// issue (part 2) -> issue
			if (bPrvIssue
				&& bIssue								// Allowed to issue.
				&& saCurRedeemReq == saCurRedeemAct		// Can only if issue if more can not be redeemed.
				&& !saPrvBalance.isNegative()			// Previous has no IOUs.
				&& saCurIssueReq != saCurIssueAct)		// Need some issued.
			{
				// Rate: quality in : 1.0
				Log(lsINFO) << str(boost::format("calcNodeAccountRev: Rate: quality in : 1.0"));

				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct);
			}

			if (!saCurRedeemAct && !saCurIssueAct)
			{
				// Must want something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
			Log(lsINFO) << str(boost::format("calcNodeAccountRev: ^|account --> ACCOUNT --> account : bPrvRedeem=%d bPrvIssue=%d bRedeem=%d bIssue=%d saCurRedeemReq=%s saCurIssueReq=%s saPrvBalance=%s saCurRedeemAct=%s saCurIssueAct=%s")
				% bPrvRedeem
				% bPrvIssue
				% bRedeem
				% bIssue
				% saCurRedeemReq.getFullText()
				% saCurIssueReq.getFullText()
				% saPrvBalance.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueAct.getFullText());
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		// Note: deliver is always issue as ACCOUNT is the issuer for the offer input.
		Log(lsINFO) << str(boost::format("calcNodeAccountRev: account --> ACCOUNT --> offer"));

		// redeem -> deliver/issue.
		if (bPrvRedeem
			&& bIssue								// Allowed to issue.
			&& saPrvBalance.isNegative()			// Previous redeeming: Previous still has IOUs.
			&& saCurDeliverReq)						// Need some issued.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct);
		}

		// issue -> deliver/issue
		if (bPrvIssue
			&& bIssue								// Allowed to issue.
			&& !saPrvBalance.isNegative()			// Previous issuing: Previous has no IOUs.
			&& saCurDeliverReq != saCurDeliverAct)	// Need some issued.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct);
		}

		if (!saCurDeliverAct)
		{
			// Must want something.
			// terResult	= tenBAD_AMOUNT;
			bSuccess	= false;
		}
	}
	else if (!bPrvAccount && bNxtAccount)
	{
		if (uIndex == uLast)
		{
			// offer --> ACCOUNT --> $
			Log(lsINFO) << str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> $"));

			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvDeliverReq, saCurWantedReq, saPrvDeliverAct, saCurWantedAct);

			if (!saCurWantedAct)
			{
				// Must have processed something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
		else
		{
			// offer --> ACCOUNT --> account
			// Note: offer is always deliver/redeeming as account is issuer.
			Log(lsINFO) << str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> account"));

			// deliver -> redeem
			if (bRedeem									// Allowed to redeem.
				&& saCurRedeemReq						// Next wants us to redeem.
				&& saPrvBalance.isNegative())			// Previous has IOUs to redeem.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct);
			}

			// deliver -> issue.
			if (bIssue									// Allowed to issue.
				&& saCurRedeemReq != saCurRedeemAct		// Can only if issue if more can not be redeemed.
				&& saPrvBalance.isNegative()			// Previous still has IOUs.
				&& saCurIssueReq)						// Need some issued.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurIssueReq, saPrvDeliverAct, saCurIssueAct);
			}

			if (!saCurDeliverAct && !saCurIssueAct)
			{
				// Must want something.
				// terResult	= tenBAD_AMOUNT;
				bSuccess	= false;
			}
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		Log(lsINFO) << str(boost::format("calcNodeAccountRev: offer --> ACCOUNT --> offer"));

		if (bIssue									// Allowed to issue.
			&& saCurDeliverReq != saCurDeliverAct)	// Can only if issue if more can not be redeemed.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct);
		}

		if (!saCurDeliverAct)
		{
			// Must want something.
			// terResult	= tenBAD_AMOUNT;
			bSuccess	= false;
		}
	}

	// XXX Need a more nuanced return: temporary fail vs perm?
	return bSuccess;
}

// The previous node: specifies what to push through to current.
// - All of previous output is consumed.
// The current node: specify what to push through to next.
// - Output to next node minus fees.
// Perform balance adjustment with previous.
bool TransactionEngine::calcNodeAccountFwd(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality)
{
	bool			bSuccess		= true;
	const unsigned int	uLast			= pspCur->vpnNodes.size() - 1;

	paymentNode&	pnPrv			= pspCur->vpnNodes[uIndex ? uIndex-1 : 0];
	paymentNode&	pnCur			= pspCur->vpnNodes[uIndex];
	paymentNode&	pnNxt			= pspCur->vpnNodes[uIndex == uLast ? uLast : uIndex+1];

	const bool		bRedeem			= !!(pnCur.uFlags & STPathElement::typeRedeem);
	const bool		bIssue			= !!(pnCur.uFlags & STPathElement::typeIssue);
	const bool		bPrvAccount		= !!(pnPrv.uFlags & STPathElement::typeAccount);
	const bool		bNxtAccount		= !!(pnNxt.uFlags & STPathElement::typeAccount);

	const uint160&	uPrvAccountID	= pnPrv.uAccountID;
	const uint160&	uCurAccountID	= pnCur.uAccountID;
	const uint160&	uNxtAccountID	= bNxtAccount ? pnNxt.uAccountID : uCurAccountID;	// Offers are always issue.

	const uint160&	uCurrencyID		= pnCur.uCurrencyID;

	uint32			uQualityIn		= rippleQualityIn(uCurAccountID, uPrvAccountID, uCurrencyID);
	uint32			uQualityOut		= rippleQualityOut(uCurAccountID, uNxtAccountID, uCurrencyID);

	// For bNxtAccount
	const STAmount&	saPrvRedeemReq	= pnPrv.saFwdRedeem;
	STAmount		saPrvRedeemAct(saPrvRedeemReq.getCurrency());

	const STAmount&	saPrvIssueReq	= pnPrv.saFwdIssue;
	STAmount		saPrvIssueAct(saPrvIssueReq.getCurrency());

	// For !bPrvAccount
	const STAmount&	saPrvDeliverReq	= pnPrv.saRevDeliver;
	STAmount		saPrvDeliverAct(saPrvDeliverReq.getCurrency());

	// For bNxtAccount
	const STAmount&	saCurRedeemReq	= pnCur.saRevRedeem;
	STAmount&		saCurRedeemAct	= pnCur.saFwdRedeem;

	const STAmount&	saCurIssueReq	= pnCur.saRevIssue;
	STAmount&		saCurIssueAct	= pnCur.saFwdIssue;

	// For !bNxtAccount
	const STAmount&	saCurDeliverReq	= pnCur.saRevDeliver;
	STAmount&		saCurDeliverAct	= pnCur.saFwdDeliver;

	STAmount&		saCurReceive	= pspCur->saOutAct;

	Log(lsINFO) << str(boost::format("calcNodeAccountFwd> uIndex=%d/%d saCurRedeemReq=%s/%s saCurIssueReq=%s/%s saCurDeliverReq=%s/%s")
		% uIndex
		% uLast
		% saCurRedeemReq.getText()
		% saCurRedeemReq.getHumanCurrency()
		% saCurIssueReq.getText()
		% saCurIssueReq.getHumanCurrency()
		% saCurDeliverReq.getText()
		% saCurDeliverReq.getHumanCurrency());

	// Ripple through account.

	if (bPrvAccount && bNxtAccount)
	{
		if (!uIndex)
		{
			// ^ --> ACCOUNT --> account

			// First node, calculate amount to send.
			// XXX Use stamp/ripple balance
			paymentNode&	pnCur			= pspCur->vpnNodes[uIndex];

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
									: MIN(saCurRedeemReq, saCurSendMaxReq);
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
									: MIN(saCurSendMaxReq-saCurRedeemAct, saCurIssueReq);
			}
			else
			{
				saCurIssueAct	= STAmount(saCurIssueReq);
			}
			saCurSendMaxAct	+= saCurIssueAct;

			Log(lsINFO) << str(boost::format("calcNodeAccountFwd: ^ --> ACCOUNT --> account : saCurSendMaxReq=%s saCurRedeemAct=%s saCurIssueReq=%s saCurIssueAct=%s")
				% saCurSendMaxReq.getFullText()
				% saCurRedeemAct.getFullText()
				% saCurIssueReq.getFullText()
				% saCurIssueAct.getFullText());
		}
		else if (uIndex == uLast)
		{
			// account --> ACCOUNT --> $
			Log(lsINFO) << str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> $ : uPrvAccountID=%s uCurAccountID=%s saPrvRedeemReq=%s saPrvIssueReq=%s")
				% NewcoinAddress::createHumanAccountID(uPrvAccountID)
				% NewcoinAddress::createHumanAccountID(uCurAccountID)
				% saPrvRedeemReq.getFullText()
				% saPrvIssueReq.getFullText());

			// Last node.  Accept all funds.  Calculate amount actually to credit.

			STAmount	saIssueCrd	= uQualityIn >= QUALITY_ONE
											? saPrvIssueReq													// No fee.
											: STAmount::multiply(saPrvIssueReq, uQualityIn, uCurrencyID);	// Fee.

			// Amount to credit.
			saCurReceive	= saPrvRedeemReq+saIssueCrd;

			// Actually receive.
			rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq+saPrvIssueReq, false);
		}
		else
		{
			// account --> ACCOUNT --> account
			Log(lsINFO) << str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> account"));

			// Previous redeem part 1: redeem -> redeem
			if (bRedeem										// Can redeem.
				&& saPrvRedeemReq != saPrvRedeemAct)		// Previous wants to redeem. To next must be ok.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvRedeemReq, saCurRedeemReq, saPrvRedeemAct, saCurRedeemAct);
			}

			// Previous redeem part 2: redeem -> issue.
			// wants to redeem and current would and can issue.
			// If redeeming cur to next is done, this implies can issue.
			if (bIssue										// Can issue.
				&& saPrvRedeemReq != saPrvRedeemAct			// Previous still wants to redeem.
				&& saCurRedeemReq == saCurRedeemAct			// Current has no more to redeem to next.
				&& saCurIssueReq)
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct);
			}

			// Previous issue part 1: issue -> redeem
			if (bRedeem										// Can redeem.
				&& saPrvIssueReq != saPrvIssueAct			// Previous wants to issue.
				&& saCurRedeemReq != saCurRedeemAct)		// Current has more to redeem to next.
			{
				// Rate: quality in : quality out
				calcNodeRipple(uQualityIn, uQualityOut, saPrvIssueReq, saCurRedeemReq, saPrvIssueAct, saCurRedeemAct);
			}

			// Previous issue part 2 : issue -> issue
			if (bIssue										// Can issue.
				&& saPrvIssueReq != saPrvIssueAct)			// Previous wants to issue. To next must be ok.
			{
				// Rate: quality in : 1.0
				calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurIssueReq, saPrvIssueAct, saCurIssueAct);
			}

			// Adjust prv --> cur balance : take all inbound
			// XXX Currency must be in amount.
			rippleCredit(uPrvAccountID, uCurAccountID, saPrvRedeemReq + saPrvIssueReq, false);
		}
	}
	else if (bPrvAccount && !bNxtAccount)
	{
		// account --> ACCOUNT --> offer
		Log(lsINFO) << str(boost::format("calcNodeAccountFwd: account --> ACCOUNT --> offer"));

		// redeem -> issue.
		// wants to redeem and current would and can issue.
		// If redeeming cur to next is done, this implies can issue.
		if (saPrvRedeemReq)								// Previous wants to redeem.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurDeliverReq, saPrvRedeemAct, saCurDeliverAct);
		}

		// issue -> issue
		if (saPrvRedeemReq == saPrvRedeemAct			// Previous done redeeming: Previous has no IOUs.
			&& saPrvIssueReq)							// Previous wants to issue. To next must be ok.
		{
			// Rate: quality in : 1.0
			calcNodeRipple(uQualityIn, QUALITY_ONE, saPrvIssueReq, saCurDeliverReq, saPrvIssueAct, saCurDeliverAct);
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
			Log(lsINFO) << str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> $"));

			// Amount to credit.
			saCurReceive	= saPrvDeliverAct;

			// No income balance adjustments necessary.  The paying side inside the offer paid to this account.
		}
		else
		{
			// offer --> ACCOUNT --> account
			Log(lsINFO) << str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> account"));

			// deliver -> redeem
			if (bRedeem										// Allowed to redeem.
				&& saPrvDeliverReq)							// Previous wants to deliver.
			{
				// Rate : 1.0 : quality out
				calcNodeRipple(QUALITY_ONE, uQualityOut, saPrvDeliverReq, saCurRedeemReq, saPrvDeliverAct, saCurRedeemAct);
			}

			// deliver -> issue
			// Wants to redeem and current would and can issue.
			if (bIssue										// Allowed to issue.
				&& saPrvDeliverReq != saPrvDeliverAct		// Previous still wants to deliver.
				&& saCurRedeemReq == saCurRedeemAct			// Current has more to redeem to next.
				&& saCurIssueReq)							// Current wants issue.
			{
				// Rate : 1.0 : transfer_rate
				calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvRedeemReq, saCurIssueReq, saPrvRedeemAct, saCurIssueAct);
			}

			// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
		}
	}
	else
	{
		// offer --> ACCOUNT --> offer
		// deliver/redeem -> deliver/issue.
		Log(lsINFO) << str(boost::format("calcNodeAccountFwd: offer --> ACCOUNT --> offer"));

		if (bIssue											// Allowed to issue.
			&& saPrvDeliverReq								// Previous wants to deliver
			&& saCurIssueReq)								// Current wants issue.
		{
			// Rate : 1.0 : transfer_rate
			calcNodeRipple(QUALITY_ONE, rippleTransferRate(uCurAccountID), saPrvDeliverReq, saCurDeliverReq, saPrvDeliverAct, saCurDeliverAct);
		}

		// No income balance adjustments necessary.  The paying side inside the offer paid and the next link will receive.
	}

	return bSuccess;
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
bool PathState::pushImply(uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID)
{
	const paymentNode&	pnPrv	= vpnNodes.back();
	bool				bValid	= true;

	Log(lsINFO) << "pushImply> "
		<< NewcoinAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< " " << NewcoinAddress::createHumanAccountID(uIssuerID);

	if (pnPrv.uCurrencyID != uCurrencyID)
	{
		// Need to convert via an offer.
#if 0
// XXX Don't know if need this.
		bool			bPrvAccount	= !!pnPrv.uAccountID;

		if (!bPrvAccount)	// Previous is an offer.
		{
			// Direct offer --> offer is not allowed for non-stamps.
			// Need to ripple through uIssuerID's account.

			bValid	= pushNode(
						STPathElement::typeAccount
							| STPathElement::typeIssue,
						pnPrv.uIssuerID,	// Intermediate account is the needed issuer.
						CURRENCY_ONE,		// Inherit from previous.
						ACCOUNT_ONE);		// Default same as account.
		}
#endif

		bValid	= pushNode(
					0,				// Offer
					ACCOUNT_ONE,	// Placeholder for offers.
					uCurrencyID,	// The offer's output is what is now wanted.
					uIssuerID);

	}

	if (bValid
		&& !!uCurrencyID					// Not stamps.
		&& (pnPrv.uAccountID != uIssuerID	// Previous is not issuing own IOUs.
			&& uAccountID != uIssuerID))	// Current is not receiving own IOUs.
	{
		// Need to ripple through uIssuerID's account.

		bValid	= pushNode(
					STPathElement::typeAccount
						| STPathElement::typeRedeem
						| STPathElement::typeIssue,
					uIssuerID,		// Intermediate account is the needed issuer.
					uCurrencyID,
					uIssuerID);
	}

	Log(lsINFO) << "pushImply< " << bValid;

	return bValid;
}

// Append a node and insert before it any implied nodes.
// <-- bValid: true, if node is valid. false, if node is malformed.
bool PathState::pushNode(int iType, uint160 uAccountID, uint160 uCurrencyID, uint160 uIssuerID)
{
	Log(lsINFO) << "pushNode> "
		<< NewcoinAddress::createHumanAccountID(uAccountID)
		<< " " << STAmount::createHumanCurrency(uCurrencyID)
		<< " " << NewcoinAddress::createHumanAccountID(uIssuerID);
	paymentNode			pnCur;
	const bool			bFirst		= vpnNodes.empty();
	const paymentNode&	pnPrv		= bFirst ? paymentNode() : vpnNodes.back();
	// true, iff node is a ripple account. false, iff node is an offer node.
	const bool			bAccount	= !!(iType & STPathElement::typeAccount);
	// true, iff currency supplied.
	// Currency is specified for the output of the current node.
	const bool			bCurrency	= !!(iType & STPathElement::typeCurrency);
	// Issuer is specified for the output of the current node.
	const bool			bIssuer		= !!(iType & STPathElement::typeIssuer);
	// true, iff account is allowed to redeem it's IOUs to next node.
	const bool			bRedeem		= !!(iType & STPathElement::typeRedeem);
	const bool			bIssue		= !!(iType & STPathElement::typeIssue);
	bool				bValid		= true;

	pnCur.uFlags		= iType;

	if (iType & ~STPathElement::typeValidBits)
	{
		Log(lsINFO) << "pushNode: bad bits.";

		bValid	= false;
	}
	else if (bAccount)
	{
		if (bRedeem || bIssue)
		{
			// Account link

			pnCur.uAccountID	= uAccountID;
			pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
			pnCur.uIssuerID		= bIssuer ? uIssuerID : uAccountID;
			pnCur.saRevRedeem	= STAmount(uCurrencyID);
			pnCur.saRevIssue	= STAmount(uCurrencyID);

			if (!bFirst)
			{
				// Add required intermediate nodes to deliver to current account.
				bValid	= pushImply(
					pnCur.uAccountID,									// Current account.
					pnCur.uCurrencyID,									// Wanted currency.
					!!pnCur.uCurrencyID ? uAccountID : ACCOUNT_XNS);	// Account as issuer.
			}

			if (bValid && !vpnNodes.empty())
			{
				const paymentNode&	pnBck		= vpnNodes.back();
				bool				bBckAccount	= !!(pnBck.uFlags & STPathElement::typeAccount);

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

						bValid	= false;
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

			if (bValid)
				vpnNodes.push_back(pnCur);
		}
		else
		{
			Log(lsINFO) << "pushNode: Account must redeem and/or issue.";

			bValid	= false;
		}
	}
	else
	{
		// Offer link
		if (bRedeem || bIssue)
		{
			bValid	= false;
		}
		else
		{
			// Offers bridge a change in currency & issuer or just a change in issuer.
			pnCur.uCurrencyID	= bCurrency ? uCurrencyID : pnPrv.uCurrencyID;
			pnCur.uIssuerID		= bIssuer ? uIssuerID : pnCur.uAccountID;

			if (!!pnPrv.uAccountID)
			{
				// Previous is an account.

				// Insert intermediary account if needed.
				bValid	= pushImply(
					!!pnPrv.uCurrencyID ? ACCOUNT_ONE : ACCOUNT_XNS,
					pnPrv.uCurrencyID,
					pnPrv.uIssuerID);
			}
			else
			{
				// Previous is an offer.
				// XXX Need code if we don't do offer to offer.
				nothing();
			}

			if (bValid)
			{
				// Verify that previous account is allowed to issue.
				const paymentNode&	pnBck		= vpnNodes.back();
				bool				bBckAccount	= !!(pnBck.uFlags & STPathElement::typeAccount);
				bool				bBckIssue	= !!(pnBck.uFlags & STPathElement::typeIssue);

				if (bBckAccount && !bBckIssue)
				{
					Log(lsINFO) << "pushNode: previous account must be allowed to issue.";

					bValid	= false;						// Malformed path.
				}
			}

			if (bValid)
				vpnNodes.push_back(pnCur);
		}
	}
	Log(lsINFO) << "pushNode< " << bValid;

	return bValid;
}

// XXX Disallow loops in ripple paths
PathState::PathState(
	Ledger::pointer			lpLedger,
	int						iIndex,
	const LedgerEntrySet&	lesSource,
	const STPath&			spSourcePath,
	uint160					uReceiverID,
	uint160					uSenderID,
	STAmount				saSend,
	STAmount				saSendMax,
	bool					bPartialPayment
	)
	: mLedger(lpLedger), mIndex(iIndex), uQuality(0)
{
	const uint160	uInCurrencyID	= saSendMax.getCurrency();
	const uint160	uOutCurrencyID	= saSend.getCurrency();
	const uint160	uInIssuerID		= !!uInCurrencyID ? uSenderID : ACCOUNT_XNS;
	const uint160	uOutIssuerID	= !!uOutCurrencyID ? uReceiverID : ACCOUNT_XNS;

	lesEntries				= lesSource.duplicate();

	saInReq					= saSendMax;
	saOutReq				= saSend;

	// Push sending node.
	bValid	= pushNode(
		STPathElement::typeAccount
			| STPathElement::typeRedeem
			| STPathElement::typeIssue
			| STPathElement::typeCurrency
			| STPathElement::typeIssuer,
		uSenderID,
		uInCurrencyID,
		uInIssuerID);

	BOOST_FOREACH(const STPathElement& speElement, spSourcePath)
	{
		if (bValid)
			bValid	= pushNode(speElement.getNodeType(), speElement.getAccountID(), speElement.getCurrency(), speElement.getIssuerID());
	}

	if (bValid)
	{
		// Create receiver node.

		bValid	= pushImply(uReceiverID, uOutCurrencyID, uOutIssuerID);
		if (bValid)
		{
			bValid	= pushNode(
				STPathElement::typeAccount						// Last node is always an account.
					| STPathElement::typeRedeem					// Does not matter just pass error check.
					| STPathElement::typeIssue,					// Does not matter just pass error check.
				uReceiverID,									// Receive to output
				uOutCurrencyID,									// Desired currency
				uOutIssuerID);
		}
	}

	Log(lsINFO) << "PathState: " << getJson();
}

Json::Value	PathState::getJson() const
{
	Json::Value	jvPathState(Json::objectValue);
	Json::Value	jvNodes(Json::arrayValue);

	BOOST_FOREACH(const paymentNode& pnNode, vpnNodes)
	{
		Json::Value	jvNode(Json::objectValue);

		Json::Value	jvFlags(Json::arrayValue);

		if (pnNode.uFlags & STPathElement::typeAccount)
			jvFlags.append("account");

		if (pnNode.uFlags & STPathElement::typeRedeem)
			jvFlags.append("redeem");

		if (pnNode.uFlags & STPathElement::typeIssue)
			jvFlags.append("issue");

		jvNode["flags"]	= jvFlags;

		if (pnNode.uFlags & STPathElement::typeAccount)
			jvNode["account"]	= NewcoinAddress::createHumanAccountID(pnNode.uAccountID);

		if (!!pnNode.uCurrencyID)
			jvNode["currency"]	= STAmount::createHumanCurrency(pnNode.uCurrencyID);

		if (!!pnNode.uIssuerID)
			jvNode["issuer"]	= NewcoinAddress::createHumanAccountID(pnNode.uIssuerID);

		// if (!!pnNode.saRevRedeem)
			jvNode["rev_redeem"]	= pnNode.saRevRedeem.getFullText();

		// if (!!pnNode.saRevIssue)
			jvNode["rev_issue"]		= pnNode.saRevIssue.getFullText();

		// if (!!pnNode.saRevDeliver)
			jvNode["rev_deliver"]	= pnNode.saRevDeliver.getFullText();

		// if (!!pnNode.saFwdRedeem)
			jvNode["fwd_redeem"]	= pnNode.saFwdRedeem.getFullText();

		// if (!!pnNode.saFwdIssue)
			jvNode["fwd_issue"]		= pnNode.saFwdIssue.getFullText();

		// if (!!pnNode.saFwdDeliver)
			jvNode["fwd_deliver"]	= pnNode.saFwdDeliver.getFullText();

		jvNodes.append(jvNode);
	}

	jvPathState["valid"]	= bValid;
	jvPathState["index"]	= mIndex;
	jvPathState["nodes"]	= jvNodes;

	if (!!saInReq)
		jvPathState["in_req"]	= saInReq.getJson(0);

	if (!!saInAct)
		jvPathState["in_act"]	= saInAct.getJson(0);

	if (!!saOutReq)
		jvPathState["out_req"]	= saOutReq.getJson(0);

	if (!!saOutAct)
		jvPathState["out_act"]	= saOutAct.getJson(0);

	if (uQuality)
		jvPathState["uQuality"]	= Json::Value::UInt(uQuality);

	return jvPathState;
}

// Calculate a node and its previous nodes.
// From the destination work towards the source calculating how much must be asked for.
// --> bAllowPartial: If false, fail if can't meet requirements.
// <-- bValid: true=success, false=insufficient funds / liqudity.
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
// XXX Disallow looping.
bool TransactionEngine::calcNode(unsigned int uIndex, PathState::pointer pspCur, bool bMultiQuality)
{
	const paymentNode&	pnCur		= pspCur->vpnNodes[uIndex];
	const bool			bCurAccount	= !!(pnCur.uFlags & STPathElement::typeAccount);
	bool				bValid;

	// Do current node reverse.
	bValid	= bCurAccount
				? calcNodeAccountRev(uIndex, pspCur, bMultiQuality)
				: calcNodeOfferRev(uIndex, pspCur, bMultiQuality);

	// Do previous.
	if (bValid && uIndex)
	{
		bValid	= calcNode(uIndex-1, pspCur, bMultiQuality);
	}

	// Do current node forward.
	if (bValid)
	{
		bValid	= bCurAccount
					? calcNodeAccountFwd(uIndex, pspCur, bMultiQuality)
					: calcNodeOfferFwd(uIndex, pspCur, bMultiQuality);
	}

	return bValid;
}

// Calculate the next increment of a path.
// The increment is what can satisfy a portion or all of the requested output at the best quality.
// <-- pspCur->uQuality
void TransactionEngine::pathNext(PathState::pointer pspCur, int iPaths)
{
	// The next state is what is available in preference order.
	// This is calculated when referenced accounts changed.

	unsigned int	uLast	= pspCur->vpnNodes.size() - 1;

	Log(lsINFO) << "Path In: " << pspCur->getJson();

	assert(pspCur->vpnNodes.size() >= 2);

	bool	bValid	= calcNode(uLast, pspCur, iPaths == 1);

	Log(lsINFO) << "pathNext: bValid="
		<< bValid
		<< " saOutAct=" << pspCur->saOutAct.getText()
		<< " saInAct=" << pspCur->saInAct.getText();

	pspCur->uQuality	= bValid
		? STAmount::getRate(pspCur->saOutAct, pspCur->saInAct)	// Calculate relative quality.
		: 0;													// Mark path as inactive.

	Log(lsINFO) << "Path Out: " << pspCur->getJson();
}

// XXX Need to audit for things like setting accountID not having memory.
TransactionEngineResult TransactionEngine::doPayment(const SerializedTransaction& txn)
{
	// Ripple if source or destination is non-native or if there are paths.
	const uint32	uTxFlags		= txn.getFlags();
	const bool		bCreate			= !!(uTxFlags & tfCreateAccount);
	const bool		bNoRippleDirect	= !!(uTxFlags & tfNoRippleDirect);
	const bool		bPartialPayment	= !!(uTxFlags & tfPartialPayment);
	const bool		bPaths			= txn.getITFieldPresent(sfPaths);
	const bool		bMax			= txn.getITFieldPresent(sfSendMax);
	const uint160	uDstAccountID	= txn.getITFieldAccount(sfDestination);
	const STAmount	saDstAmount		= txn.getITFieldAmount(sfAmount);
	const STAmount	saMaxAmount		= bMax ? txn.getITFieldAmount(sfSendMax) : saDstAmount;
	const uint160	uSrcCurrency	= saMaxAmount.getCurrency();
	const uint160	uDstCurrency	= saDstAmount.getCurrency();

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doPayment: Invalid transaction: Payment destination account not specifed.";

		return tenDST_NEEDED;
	}
	else if (!saDstAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad amount: " << saDstAmount.getHumanCurrency() << " " << saDstAmount.getText();

		return tenBAD_AMOUNT;
	}
	else if (mTxnAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Invalid transaction: Redunant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% mTxnAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return tenREDUNDANT;
	}
	else if (bMax
		&& ((saMaxAmount == saDstAmount && saMaxAmount.getCurrency() == saDstAmount.getCurrency())
			|| (saDstAmount.isNative() && saMaxAmount.isNative())))
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad SendMax.";

		return tenINVALID;
	}

	SLE::pointer		sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));
	if (!sleDst)
	{
		// Destination account does not exist.
		if (bCreate && !saDstAmount.isNative())
		{
			// This restriction could be relaxed.
			Log(lsINFO) << "doPayment: Invalid transaction: Create account may only fund XNS.";

			return tenCREATEXNS;
		}
		else if (!bCreate)
		{
			Log(lsINFO) << "doPayment: Delay transaction: Destination account does not exist.";

			return terNO_DST;
		}

		// Create the account.
		sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

		sleDst->setIFieldU32(sfSequence, 1);
	}
	else
	{
		entryModify(sleDst);
	}

	// XXX Should bMax be sufficient to imply ripple?
	bool		bRipple			= bPaths || bMax || !saDstAmount.isNative();

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

		return terSUCCESS;
	}

	//
	// Ripple payment
	//

#if 0
// Disabled to make sure full ripple code is correct.
	// Try direct ripple first.
	if (!bNoRippleDirect && mTxnAccountID != uDstAccountID && uSrcCurrency == uDstCurrency)
	{
		// XXX Does not handle quality.
		SLE::pointer		sleRippleState	= entryCache(ltRIPPLE_STATE, Ledger::getRippleStateIndex(mTxnAccountID, uDstAccountID, uDstCurrency));

		if (sleRippleState)
		{
			// There is a direct credit-line of some direction.
			// - We can always pay IOUs back.
			// - We can issue IOUs to the limit.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bSendHigh		= uLowID == mTxnAccountID && uHighID == uDstAccountID;
			bool		bSendLow		= uLowID == uDstAccountID && uHighID == mTxnAccountID;
			// Flag we need if we end up owing IOUs.
			uint32		uFlags			= bSendHigh ? lsfLowIndexed : lsfHighIndexed;

			assert(bSendLow || bSendHigh);

			STAmount	saDstLimit		= sleRippleState->getIValueFieldAmount(bSendLow ? sfLowLimit : sfHighLimit);

			STAmount	saDstBalance	= sleRippleState->getIValueFieldAmount(sfBalance);
				if (bSendHigh)
				{
					// Put balance in dst terms.
					saDstBalance.negate();
				}

			saDstBalance += saDstAmount;
			if (saDstBalance > saDstLimit)
			{
				// Would exceed credit limit.
				// YYY Note: in the future could push out other credits to make payment fit.

				Log(lsINFO) << "doPayment: Delay transaction: Over limit: proposed balance="
					<< saDstBalance.getText()
					<< " limit="
					<< saDstLimit.getText();

				return terOVER_LIMIT;
			}

			if (saDstBalance.isZero())
			{
				// XXX May be able to delete indexes for credit limits which are zero.
				nothing();
			}
			else if (saDstBalance.isNegative())
			{
				// dst still has outstanding IOUs, sle already indexed.
				nothing();
			}
			// src has outstanding IOUs, sle should be indexed.
			else if (! (sleRippleState->getFlags() & uFlags))
			{
				// Need to add index.
				TransactionEngineResult	terResult	= terSUCCESS;
				uint64					uSrcRef;					// Ignored, ripple_state dirs never delete.

				terResult	= dirAdd(
					uSrcRef,
					Ledger::getOwnerDirIndex(mTxnAccountID),		// The source ended up owing.
					sleRippleState->getIndex());					// Adding current entry.

				if (terSUCCESS != terResult)
					return terResult;

				sleRippleState->setFlag(uFlags);					// Note now indexed.
			}

			if (bSendHigh)
			{
				// Put balance in low terms.
				saDstBalance.negate();
			}

			sleRippleState->setIFieldAmount(sfBalance, saDstBalance);
			entryModify(sleRippleState);

			return terSUCCESS;
		}
	}
#endif

	STPathSet	spsPaths = txn.getITFieldPathSet(sfPaths);

	if (bNoRippleDirect && spsPaths.isEmpty())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: No paths and direct ripple not allowed.";

		return tenRIPPLE_EMPTY;
	}

	if (spsPaths.getPathCount() > RIPPLE_PATHS_MAX)
	{
		return tenBAD_PATH_COUNT;
	}

	// Incrementally search paths.
	std::vector<PathState::pointer>	vpsPaths;

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
			saMaxAmount,
			bPartialPayment);

		if (pspDirect)
			vpsPaths.push_back(pspDirect);
	}

	Log(lsINFO) << "doPayment: Paths: " << spsPaths.getPathCount();

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
			saMaxAmount,
			bPartialPayment);

		if (pspExpanded)
			vpsPaths.push_back(pspExpanded);
	}

	TransactionEngineResult	terResult;
	STAmount				saPaid;
	STAmount				saWanted;

	terResult	= tenUNKNOWN;
	while (tenUNKNOWN == terResult)
	{
		PathState::pointer	pspBest;
		LedgerEntrySet		lesCheckpoint	= mNodes;

		// Find the best path.
		BOOST_FOREACH(PathState::pointer pspCur, vpsPaths)
		{
			mNodes	= lesCheckpoint;					// Restore from checkpoint.
			mNodes.bumpSeq();							// Begin ledger varance.

			pathNext(pspCur, vpsPaths.size());			// Compute increment

			mNodes.swapWith(pspCur->lesEntries);		// For the path, save ledger state.

			if (!pspBest || (pspCur->uQuality && PathState::lessPriority(pspBest, pspCur)))
				pspBest	= pspCur;
		}

		if (pspBest)
		{
			// Apply best path.

			// Install ledger for best past.
			mNodes.swapWith(pspBest->lesEntries);

			// Figure out if done.
			if (tenUNKNOWN == terResult && saPaid == saWanted)
			{
				terResult	= terSUCCESS;
			}
		}
		// Ran out of paths.
		else if (!bPartialPayment)
		{
			// Partial payment not allowed.
			terResult	= terPATH_PARTIAL;		// XXX No effect, except unfunded and charge fee.
		}
		else if (saPaid.isZero())
		{
			// Nothing claimed.
			terResult	= terPATH_EMPTY;		// XXX No effect except unfundeds and charge fee.
		}
		else
		{
			terResult	= terSUCCESS;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	if (transResultInfo(terResult, strToken, strHuman))
	{
		Log(lsINFO) << str(boost::format("doPayment: %s: %s") % strToken % strHuman);
	}
	else
	{
		assert(false);
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doWalletAdd(const SerializedTransaction& txn)
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

		return tenBAD_ADD_AUTH;
	}

	SLE::pointer		sleDst	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tenCREATED;
	}

	STAmount			saAmount		= txn.getITFieldAmount(sfAmount);
	STAmount			saSrcBalance	= mTxnAccount->getIValueFieldAmount(sfBalance);

	if (saSrcBalance < saAmount)
	{
		std::cerr
			<< str(boost::format("WalletAdd: Delay transaction: insufficent balance: balance=%s amount=%s")
				% saSrcBalance.getText()
				% saAmount.getText())
			<< std::endl;

		return terUNFUNDED;
	}

	// Deduct initial balance from source account.
	mTxnAccount->setIFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	sleDst->setIFieldU32(sfSequence, 1);
	sleDst->setIFieldAmount(sfBalance, saAmount);
	sleDst->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	std::cerr << "WalletAdd<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doInvoice(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

// Take as much as possible. Adjusts account balances. Charges fees on top to taker.
// -->   uBookBase: The order book to take against.
// --> saTakerPays: What the taker offers (w/ issuer)
// --> saTakerGets: What the taker wanted (w/ issuer)
// <-- saTakerPaid: What taker paid not including fees. To reduce an offer.
// <--  saTakerGot: What taker got not including fees. To reduce an offer.
// <--   terResult: terSUCCESS or terNO_ACCOUNT
// Note: All SLE modifications must always occur even on failure.
// XXX: Fees should be paid by the source of the currency.
TransactionEngineResult TransactionEngine::takeOffers(
	bool				bPassive,
	const uint256&		uBookBase,
	const uint160&		uTakerAccountID,
	const SLE::pointer&	sleTakerAccount,
	const STAmount&		saTakerPays,
	const STAmount&		saTakerGets,
	STAmount&			saTakerPaid,
	STAmount&			saTakerGot)
{
	assert(!saTakerPays.isZero() && !saTakerGets.isZero());

	Log(lsINFO) << "takeOffers: against book: " << uBookBase.ToString();

	uint256					uTipIndex			= uBookBase;
	const uint256			uBookEnd			= Ledger::getQualityNext(uBookBase);
	const uint64			uTakeQuality		= STAmount::getRate(saTakerGets, saTakerPays);
	const uint160			uTakerPaysAccountID	= saTakerPays.getIssuer();
	const uint160			uTakerPaysCurrency	= saTakerPays.getCurrency();
	const uint160			uTakerGetsAccountID	= saTakerGets.getIssuer();
	const uint160			uTakerGetsCurrency	= saTakerGets.getCurrency();
	TransactionEngineResult	terResult			= tenUNKNOWN;

	saTakerPaid	= 0;
	saTakerGot	= 0;

	while (tenUNKNOWN == terResult)
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

		if (!sleOfferDir || uTakeQuality < uTipQuality || (bPassive && uTakeQuality == uTipQuality))
		{
			// Done.
			Log(lsINFO) << "takeOffers: done";

			terResult	= terSUCCESS;
		}
		else
		{
			// Have an offer to consider.
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

			if (sleOffer->getIFieldPresent(sfGetsIssuer))
				saOfferPays.setIssuer(sleOffer->getIValueFieldAccount(sfGetsIssuer).getAccountID());

			if (sleOffer->getIFieldPresent(sfPaysIssuer))
				saOfferGets.setIssuer(sleOffer->getIValueFieldAccount(sfPaysIssuer).getAccountID());

			if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
			{
				// Offer is expired. Delete it.
				Log(lsINFO) << "takeOffers: encountered expired offer";

				offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);

				mUnfunded.insert(uOfferIndex);
			}
			else if (uOfferOwnerID == uTakerAccountID)
			{
				// Would take own offer. Consider old offer unfunded.
				Log(lsINFO) << "takeOffers: encountered taker's own old offer";

				offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);
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

					offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);

					mUnfunded.insert(uOfferIndex);
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
					if (bOfferDelete)
					{
						// Offer now fully claimed or now unfunded.
						Log(lsINFO) << "takeOffers: offer claimed: delete";

						offerDelete(sleOffer, uOfferIndex, uOfferOwnerID);
					}
					else
					{
						Log(lsINFO) << "takeOffers: offer partial claim: modify";

						// Offer owner will pay less.  Subtract what taker just got.
						sleOffer->setIFieldAmount(sfTakerGets, saOfferPays -= saSubTakerGot);

						// Offer owner will get less.  Subtract what owner just paid.
						sleOffer->setIFieldAmount(sfTakerPays, saOfferGets -= saSubTakerPaid);

						entryModify(sleOffer);
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

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCreate(const SerializedTransaction& txn)
{
Log(lsWARNING) << "doOfferCreate> " << txn.getJson(0);
	const uint32			txFlags			= txn.getFlags();
	const bool				bPassive		= !!(txFlags & tfPassive);
	const uint160			uPaysIssuerID	= txn.getITFieldAccount(sfPaysIssuer);
	const uint160			uGetsIssuerID	= txn.getITFieldAccount(sfGetsIssuer);
	STAmount			saTakerPays		= txn.getITFieldAmount(sfTakerPays);
		saTakerPays.setIssuer(uPaysIssuerID);
Log(lsWARNING) << "doOfferCreate: saTakerPays=" << saTakerPays.getFullText();
	STAmount				saTakerGets		= txn.getITFieldAmount(sfTakerGets);
		saTakerGets.setIssuer(uGetsIssuerID);
Log(lsWARNING) << "doOfferCreate: saTakerGets=" << saTakerGets.getFullText();
	const uint32			uExpiration		= txn.getITFieldU32(sfExpiration);
	const bool				bHaveExpiration	= txn.getITFieldPresent(sfExpiration);
	const uint32			uSequence		= txn.getSequence();

	const uint256			uLedgerIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer			sleOffer		= entryCreate(ltOFFER, uLedgerIndex);

	Log(lsINFO) << "doOfferCreate: Creating offer node: " << uLedgerIndex.ToString() << " uSequence=" << uSequence;

	const uint160			uPaysCurrency	= saTakerPays.getCurrency();
	const uint160			uGetsCurrency	= saTakerGets.getCurrency();
	const uint64			uRate			= STAmount::getRate(saTakerGets, saTakerPays);

	TransactionEngineResult	terResult		= terSUCCESS;
	uint256					uDirectory;		// Delete hints.
	uint64					uOwnerNode;
	uint64					uBookNode;

	if (bHaveExpiration && !uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad expiration";

		terResult	= tenBAD_EXPIRATION;
	}
	else if (bHaveExpiration && mLedger->getParentCloseTimeNC() >= uExpiration)
	{
		Log(lsWARNING) << "doOfferCreate: Expired transaction: offer expired";

		terResult	= tenEXPIRED;
	}
	else if (saTakerPays.isNative() && saTakerGets.isNative())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: XNS for XNS";

		terResult	= tenBAD_OFFER;
	}
	else if (saTakerPays.isZero() || saTakerGets.isZero())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad amount";

		terResult	= tenBAD_OFFER;
	}
	else if (uPaysCurrency == uGetsCurrency && uPaysIssuerID == uGetsIssuerID)
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: redundant offer";

		terResult	= tenREDUNDANT;
	}
	else if (saTakerPays.isNative() != uPaysIssuerID.isZero() || saTakerGets.isNative() != uGetsIssuerID.isZero())
	{
		Log(lsWARNING) << "doOfferCreate: Malformed offer: bad issuer";

		terResult	= tenBAD_ISSUER;
	}
	else if (!accountFunds(mTxnAccountID, saTakerGets).isPositive())
	{
		Log(lsWARNING) << "doOfferCreate: delay: offers must be funded";

		terResult	= terUNFUNDED;
	}

	if (terSUCCESS == terResult && !saTakerPays.isNative())
	{
		SLE::pointer		sleTakerPays	= entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uPaysIssuerID));

		if (!sleTakerPays)
		{
			Log(lsWARNING) << "doOfferCreate: delay: can't receive IOUs from non-existant issuer: " << NewcoinAddress::createHumanAccountID(uPaysIssuerID);

			terResult	= terNO_ACCOUNT;
		}
	}

	if (terSUCCESS == terResult)
	{
		STAmount		saOfferPaid;
		STAmount		saOfferGot;
		const uint256	uTakeBookBase	= Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID);

		Log(lsINFO) << str(boost::format("doOfferCreate: take against book: %s : %s/%s -> %s/%s")
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

		if (terSUCCESS == terResult)
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

	if (terSUCCESS == terResult
		&& !saTakerPays.isZero()						// Still wanting something.
		&& !saTakerGets.isZero()						// Still offering something.
		&& accountFunds(mTxnAccountID, saTakerGets).isPositive())	// Still funded.
	{
		// We need to place the remainder of the offer into its order book.

		// Add offer to owner's directory.
		terResult	= dirAdd(uOwnerNode, Ledger::getOwnerDirIndex(mTxnAccountID), uLedgerIndex);

		if (terSUCCESS == terResult)
		{
			uint256	uBookBase	= Ledger::getBookBase(uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID);

			Log(lsINFO) << str(boost::format("doOfferCreate: adding to book: %s : %s/%s -> %s/%s")
				% uBookBase.ToString()
				% saTakerPays.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerPays.getIssuer())
				% saTakerGets.getHumanCurrency()
				% NewcoinAddress::createHumanAccountID(saTakerGets.getIssuer()));

			uDirectory	= Ledger::getQualityIndex(uBookBase, uRate);	// Use original rate.

			// Add offer to order book.
			terResult	= dirAdd(uBookNode, uDirectory, uLedgerIndex);
		}

		if (terSUCCESS == terResult)
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

			if (!saTakerPays.isNative())
				sleOffer->setIFieldAccount(sfPaysIssuer, uPaysIssuerID);

			if (!saTakerGets.isNative())
				sleOffer->setIFieldAccount(sfGetsIssuer, uGetsIssuerID);

			if (uExpiration)
				sleOffer->setIFieldU32(sfExpiration, uExpiration);

			if (bPassive)
				sleOffer->setFlag(lsfPassive);
		}
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCancel(const SerializedTransaction& txn)
{
	TransactionEngineResult	terResult;
	const uint32			uSequence	= txn.getITFieldU32(sfOfferSequence);
	const uint256			uOfferIndex	= Ledger::getOfferIndex(mTxnAccountID, uSequence);
	SLE::pointer			sleOffer	= entryCache(ltOFFER, uOfferIndex);

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

TransactionEngineResult TransactionEngine::doTake(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doStore(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

TransactionEngineResult TransactionEngine::doDelete(const SerializedTransaction& txn)
{
	return tenUNKNOWN;
}

// vim:ts=4
