//
// XXX Should make sure all fields and are recognized on a transactions.
// XXX Make sure fee is claimed for failed transactions.
// XXX Might uses an unordered set for vector.
//

#include "TransactionEngine.h"

#include <boost/foreach.hpp>
#include <boost/format.hpp>

#include "../json/writer.h"

#include "Config.h"
#include "TransactionFormats.h"
#include "utils.h"
#include "Log.h"

// Small for testing, should likely be 32 or 64.
#define DIR_NODE_MAX	2

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
		{	terCREATED,				"terCREATED",				"Can not create a previously created account."		},
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

STAmount TransactionEngine::rippleBalance(const uint160& uAccountID, const uint160& uIssuerAccountID, const uint160& uCurrency)
{
	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleRippleState	= mLedger->getRippleState(qry, Ledger::getRippleStateIndex(uAccountID, uIssuerAccountID, uCurrency));

	STAmount	saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);
	if (uAccountID < uIssuerAccountID)
		saBalance.negate();		// Put balance in low terms.

	return saBalance;
}

void TransactionEngine::rippleCredit(const uint160& uAccountID, const uint160& uIssuerAccountID, const uint160& uCurrency, const STAmount& saCredit)
{
	uint256				uIndex			= Ledger::getRippleStateIndex(uAccountID, uIssuerAccountID, uCurrency);
	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleRippleState	= mLedger->getRippleState(qry, uIndex);
	bool				bFlipped		= uAccountID > uIssuerAccountID;

	if (!sleRippleState)
	{
		STAmount	saBalance	= saCredit;

		sleRippleState	= entryCreate(ltRIPPLE_STATE, uIndex);

		Log(lsINFO) << "rippleCredit: Creating ripple line: " << uIndex.ToString();

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setIFieldAmount(sfBalance, saBalance);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uIssuerAccountID);
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, uAccountID);
	}
	else
	{
		STAmount	saBalance	= sleRippleState->getIValueFieldAmount(sfBalance);

		if (!bFlipped)
			saBalance.negate();		// Put balance in low terms.

		saBalance	+= saCredit;

		if (!bFlipped)
			saBalance.negate();

		sleRippleState->setIFieldAmount(sfBalance, saBalance);

		entryModify(sleRippleState);
	}
}

void TransactionEngine::rippleDebit(const uint160& uAccountID, const uint160& uIssuerAccountID, const uint160& uCurrency, const STAmount& saDebit)
{
	STAmount	saCredit	= saDebit;

	saCredit.negate();

	rippleCredit(uAccountID, uIssuerAccountID, uCurrency, saCredit);
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
	LedgerStateParms	lspRoot		= lepNONE;
	SLE::pointer		sleRoot		= mLedger->getDirNode(lspRoot, uRootIndex);

	if (!sleRoot)
	{
		// No root, make it.
		sleRoot		= entryCreate(ltDIR_NODE, uRootIndex);

		sleNode		= sleRoot;
		uNodeDir	= 0;
	}
	else
	{
		uNodeDir		= sleRoot->getIFieldU64(sfIndexPrevious);		// Get index to last directory node.

		if (uNodeDir)
		{
			// Try adding to last node.
			lspRoot		= lepNONE;
			sleNode		= mLedger->getDirNode(lspRoot, Ledger::getDirNodeIndex(uRootIndex, uNodeDir));

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

								lspRoot		= lepNONE;
				SLE::pointer	slePrevious	= mLedger->getDirNode(lspRoot, Ledger::getDirNodeIndex(uRootIndex, uNodeDir-1));

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
	LedgerStateParms	lspNode		= lepNONE;
	SLE::pointer		sleNode		= mLedger->getDirNode(lspNode, uNodeCur ? Ledger::getDirNodeIndex(uRootIndex, uNodeCur) : uRootIndex);

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
				LedgerStateParms		lspLast	= lepNONE;
				SLE::pointer			sleLast	= mLedger->getDirNode(lspLast, Ledger::getDirNodeIndex(uRootIndex, uNodeNext));

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

			LedgerStateParms	lspPrevious	= lepNONE;
			SLE::pointer		slePrevious	= mLedger->getDirNode(lspPrevious, uNodePrevious ? Ledger::getDirNodeIndex(uRootIndex, uNodePrevious) : uRootIndex);

			assert(slePrevious);

			LedgerStateParms	lspNext		= lepNONE;
			SLE::pointer		sleNext		= mLedger->getDirNode(lspNext, uNodeNext ? Ledger::getDirNodeIndex(uRootIndex, uNodeNext) : uRootIndex);

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
			LedgerStateParms		lspRoot	= lepNONE;
			SLE::pointer			sleRoot	= mLedger->getDirNode(lspRoot, uRootIndex);

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

// Set the authorized public ket for an account.  May also set the generator map.
TransactionEngineResult	TransactionEngine::setAuthorized(const SerializedTransaction& txn, SLE::pointer sleSrc, bool bMustSetGenerator)
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

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleGen			= mLedger->getGenerator(qry, hGeneratorID);
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

	sleSrc->setIFieldAccount(sfAuthorizedKey, uAuthKeyID);

	return terSUCCESS;
}

Ledger::pointer TransactionEngine::getTransactionLedger(uint32 targetLedger)
{
	Ledger::pointer ledger = mDefaultLedger;
	if (mAlternateLedger && (targetLedger != 0) &&
		(targetLedger != mLedger->getLedgerSeq()) && (targetLedger == mAlternateLedger->getLedgerSeq()))
	{
		Log(lsINFO) << "Transaction goes into wobble ledger";
		ledger = mAlternateLedger;
	}
	return ledger;
}

bool TransactionEngine::entryExists(SLE::pointer sleEntry)
{
	return mEntries.find(sleEntry) != mEntries.end();
}

SLE::pointer TransactionEngine::entryCreate(LedgerEntryType letType, const uint256& uIndex)
{
	assert(!uIndex.isZero());

	SLE::pointer	sleNew	= boost::make_shared<SerializedLedgerEntry>(letType);

	sleNew->setIndex(uIndex);

	mEntries[sleNew]	= taaCREATE;

	return sleNew;
}

void TransactionEngine::entryDelete(SLE::pointer sleEntry)
{
	assert(sleEntry);
	entryMap::const_iterator	it	= mEntries.find(sleEntry);

	switch (it == mEntries.end() ? taaNONE : it->second)
	{
		case taaCREATE:
		case taaUNFUNDED:
			assert(false);						// Unexpected case.
			break;

		case taaMODIFY:
		case taaNONE:
			mEntries[sleEntry]	= taaDELETE;	// Upgrade.
			break;

		case taaDELETE:
			nothing();							// No change.
			break;
	}
}

void TransactionEngine::entryModify(SLE::pointer sleEntry)
{
	assert(sleEntry);
	entryMap::const_iterator	it	= mEntries.find(sleEntry);

	switch (it == mEntries.end() ? taaNONE : it->second)
	{
		case taaUNFUNDED:
		case taaDELETE:
			assert(false);						// Unexpected case.
			break;

		case taaNONE:
			mEntries[sleEntry]	= taaMODIFY;	// Upgrade.
			break;

		case taaCREATE:
		case taaMODIFY:
			nothing();							// No change.
			break;
	}
}

void TransactionEngine::entryUnfunded(SLE::pointer sleEntry)
{
	assert(sleEntry);
	entryMap::const_iterator	it	= mEntries.find(sleEntry);

	switch (it == mEntries.end() ? taaNONE : it->second)
	{
		case taaCREATE:
		case taaMODIFY:
		case taaDELETE:
			assert(false);						// Unexpected case.
			break;

		case taaNONE:
			mEntries[sleEntry]	= taaUNFUNDED;	// Upgrade.
			break;

		case taaUNFUNDED:
			nothing();							// No change.
			break;
	}
}

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params, uint32 targetLedger)
{
	return applyTransaction(txn, params, getTransactionLedger(targetLedger));
}

TransactionEngineResult TransactionEngine::applyTransaction(const SerializedTransaction& txn,
	TransactionEngineParams params, Ledger::pointer ledger)
{
	Log(lsTRACE) << "applyTransaction>";
	mLedger					= ledger;
	mLedgerParentCloseTime	= mLedger->getParentCloseTimeNC();

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
	if (terSUCCESS == terResult && !txn.checkSign(naSigningPubKey))
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
					LedgerStateParms	qry				= lepNONE;
					SLE::pointer		sleNickname		= mLedger->getNickname(qry, txn.getITFieldH256(sfNickname));

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
	uint160 srcAccountID = txn.getSourceAccount().getAccountID();
	if (terSUCCESS == terResult && !srcAccountID)
	{
		Log(lsWARNING) << "applyTransaction: bad source id";

		terResult	= tenINVALID;
	}

	if (terSUCCESS != terResult)
	{
		// Avoid unnecessary locking.
		mLedger = Ledger::pointer();

		return terResult;
	}

	boost::recursive_mutex::scoped_lock sl(mLedger->mLock);

	// find source account
	// If are only forwarding, due to resource limitations, we might verifying only some transactions, this would be probablistic.

	STAmount			saSrcBalance;
	uint32				t_seq			= txn.getSequence();
	LedgerStateParms	lspRoot			= lepNONE;
	SLE::pointer		sleSrc			= mLedger->getAccountRoot(lspRoot, srcAccountID);
	bool				bHaveAuthKey	= false;

	if (!sleSrc)
	{
		Log(lsTRACE) << str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			txn.getSourceAccount().humanAccountID());

		terResult			= terNO_ACCOUNT;
	}
	else
	{
		saSrcBalance	= sleSrc->getIValueFieldAmount(sfBalance);
		bHaveAuthKey	= sleSrc->getIFieldPresent(sfAuthorizedKey);
	}

	// Check if account cliamed.
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
				if (naSigningPubKey.getAccountID() != srcAccountID)
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
				if (naSigningPubKey.getAccountID() != srcAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					Log(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					Log(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= tenBAD_SET_ID;
				}
				break;

			default:
				// Verify the transaction's signing public key is the key authorized for signing.
				if (bHaveAuthKey && naSigningPubKey.getAccountID() == sleSrc->getIValueFieldAccount(sfAuthorizedKey).getAccountID())
				{
					// Authorized to continue.
					nothing();
				}
				else if (naSigningPubKey.getAccountID() == srcAccountID)
				{
					// Authorized to continue.
					nothing();
				}
				else if (bHaveAuthKey)
				{
					std::cerr << "applyTransaction: Delay: Not authorized to use account." << std::endl;

					terResult	= terBAD_AUTH;
				}
				else
				{
					std::cerr << "applyTransaction: Invalid: Not authorized to use account." << std::endl;

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
		std::cerr
			<< str(boost::format("applyTransaction: Delay: insufficent balance: balance=%s paid=%s")
				% saSrcBalance.getText()
				% saPaid.getText())
			<< std::endl;

		terResult	= terINSUF_FEE_B;
	}
	else
	{
		sleSrc->setIFieldAmount(sfBalance, saSrcBalance - saPaid);
	}

	// Validate sequence
	if (terSUCCESS != terResult)
	{
		nothing();
	}
	else if (!saCost.isZero())
	{
		uint32 a_seq = sleSrc->getIFieldU32(sfSequence);

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
			sleSrc->setIFieldU32(sfSequence, t_seq + 1);
		}
	}
	else
	{
		Log(lsINFO) << "applyTransaction: Zero cost transaction";

		if (t_seq)
		{
			std::cerr << "applyTransaction: bad sequence for pre-paid transaction" << std::endl;

			terResult	= terPAST_SEQ;
		}
	}

	if (terSUCCESS == terResult)
	{
		entryModify(sleSrc);

		switch(txn.getTxnType())
		{
			case ttACCOUNT_SET:
				terResult = doAccountSet(txn, sleSrc);
				break;

			case ttCLAIM:
				terResult = doClaim(txn, sleSrc);
				break;

			case ttCREDIT_SET:
				terResult = doCreditSet(txn, srcAccountID);
				break;

			case ttINVALID:
				std::cerr << "applyTransaction: invalid type" << std::endl;
				terResult = tenINVALID;
				break;

			case ttINVOICE:
				terResult = doInvoice(txn);
				break;

			case ttOFFER_CREATE:
				terResult = doOfferCreate(txn, sleSrc, srcAccountID);
				break;

			case ttOFFER_CANCEL:
				terResult = doOfferCancel(txn, srcAccountID);
				break;

			case ttNICKNAME_SET:
				terResult = doNicknameSet(txn, sleSrc, srcAccountID);
				break;

			case ttPASSWORD_FUND:
				terResult = doPasswordFund(txn, sleSrc, srcAccountID);
				break;

			case ttPASSWORD_SET:
				terResult = doPasswordSet(txn, sleSrc);
				break;

			case ttPAYMENT:
				terResult = doPayment(txn, sleSrc, srcAccountID);
				break;

			case ttWALLET_ADD:
				terResult = doWalletAdd(txn, sleSrc);
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

	bool	bWrite	= false;

	if (terSUCCESS != terResult)
	{
		BOOST_FOREACH(entryMap_value_type it, mEntries)
		{
			const SLE::pointer&	sleEntry	= it.first;

			switch (it.second)
			{
				case taaNONE:
					assert(false);
					break;

				case taaUNFUNDED:
					{
						Log(lsINFO) << "applyTransaction: taaUNFUNDED: " << sleEntry->getText();

						if (!mLedger->peekAccountStateMap()->delItem(sleEntry->getIndex()))
							assert(false);

						bWrite	= true;
					}
					break;

				case taaCREATE:
				case taaMODIFY:
				case taaDELETE:
					nothing();
					break;
			}
		}
	}

	if (terSUCCESS == terResult)
	{ // Write back the account states and add the transaction to the ledger
		bWrite	= true;

		BOOST_FOREACH(entryMap_value_type it, mEntries)
		{
			const SLE::pointer&	sleEntry	= it.first;

			switch (it.second)
			{
				case taaNONE:
					assert(false);
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

						if (!mLedger->peekAccountStateMap()->delItem(sleEntry->getIndex()))
							assert(false);
					}
					break;

				case taaUNFUNDED:
					{
						Log(lsINFO) << "applyTransaction: taaUNFUNDED: " << sleEntry->getText();

						if (!mLedger->peekAccountStateMap()->delItem(sleEntry->getIndex()))
							assert(false);
					}
					break;
			}
		}
	}

	if (bWrite)
	{
		Serializer s;

		txn.add(s);

		if (!mLedger->addTransaction(txID, s))
			assert(false);

		if ((params & tepUPDATE_TOTAL) != tepNONE)
			mLedger->destroyCoins(saPaid.getNValue());
	}

	mLedger = Ledger::pointer();
	mEntries.clear();

	return terResult;
}

TransactionEngineResult TransactionEngine::doAccountSet(const SerializedTransaction& txn, SLE::pointer sleSrc)
{
	std::cerr << "doAccountSet>" << std::endl;

	uint32				txFlags			= txn.getFlags();

	//
	// EmailHash
	//

	if (txFlags & tfUnsetEmailHash)
	{
		std::cerr << "doAccountSet: unset email hash" << std::endl;

		sleSrc->makeIFieldAbsent(sfEmailHash);
	}
	else if (txn.getITFieldPresent(sfEmailHash))
	{
		std::cerr << "doAccountSet: set email hash" << std::endl;

		sleSrc->setIFieldH128(sfEmailHash, txn.getITFieldH128(sfEmailHash));
	}

	//
	// WalletLocator
	//

	if (txFlags & tfUnsetWalletLocator)
	{
		std::cerr << "doAccountSet: unset wallet locator" << std::endl;

		sleSrc->makeIFieldAbsent(sfWalletLocator);
	}
	else if (txn.getITFieldPresent(sfWalletLocator))
	{
		std::cerr << "doAccountSet: set wallet locator" << std::endl;

		sleSrc->setIFieldH256(sfWalletLocator, txn.getITFieldH256(sfWalletLocator));
	}

	//
	// MessageKey
	//

	if (!txn.getITFieldPresent(sfMessageKey))
	{
		nothing();

	}
	else if (sleSrc->getIFieldPresent(sfMessageKey))
	{
		std::cerr << "doAccountSet: can not change message key" << std::endl;

		return tenMSG_SET;
	}
	else
	{
		std::cerr << "doAccountSet: set message key" << std::endl;

		sleSrc->setIFieldVL(sfMessageKey, txn.getITFieldVL(sfMessageKey));
	}

	std::cerr << "doAccountSet<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doClaim(const SerializedTransaction& txn, SLE::pointer sleSrc)
{
	std::cerr << "doClaim>" << std::endl;

	TransactionEngineResult	terResult	= setAuthorized(txn, sleSrc, true);

	std::cerr << "doClaim<" << std::endl;

	return terResult;
}

TransactionEngineResult TransactionEngine::doCreditSet(const SerializedTransaction& txn, const uint160& uSrcAccountID)
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
	else if (uSrcAccountID == uDstAccountID)
	{
		Log(lsINFO) << "doCreditSet: Invalid transaction: Can not extend credit to self.";

		return tenDST_IS_SRC;
	}

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleDst			= mLedger->getAccountRoot(qry, uDstAccountID);
	if (!sleDst)
	{
		Log(lsINFO) << "doCreditSet: Delay transaction: Destination account does not exist.";

		return terNO_DST;
	}

	STAmount			saLimitAmount	= txn.getITFieldAmount(sfLimitAmount);
	uint160				uCurrency		= saLimitAmount.getCurrency();
	bool				bFlipped		= uSrcAccountID > uDstAccountID;
	uint32				uFlags			= bFlipped ? lsfLowIndexed : lsfHighIndexed;
	STAmount			saBalance(uCurrency);
	bool				bAddIndex		= false;
	bool				bDelIndex		= false;

						qry				= lepNONE;
	SLE::pointer		sleRippleState	= mLedger->getRippleState(qry, uSrcAccountID, uDstAccountID, uCurrency);
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
				terResult	= dirDelete(true, uSrcRef, Ledger::getRippleDirIndex(uSrcAccountID), sleRippleState->getIndex());
			}
		}
#endif
		if (!bDelIndex)
		{
			bAddIndex		= !(sleRippleState->getFlags() & uFlags);

			sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit: sfLowLimit , saLimitAmount);

			if (bAddIndex)
				sleRippleState->setFlag(uFlags);

			entryModify(sleRippleState);
		}

		Log(lsINFO) << "doCreditSet: Modifying ripple line: bAddIndex=" << bAddIndex << " bDelIndex=" << bDelIndex;
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
		STAmount		saZero(uCurrency);

		bAddIndex		= true;
		sleRippleState	= entryCreate(ltRIPPLE_STATE, Ledger::getRippleStateIndex(uSrcAccountID, uDstAccountID, uCurrency));

		Log(lsINFO) << "doCreditSet: Creating ripple line: " << sleRippleState->getIndex().ToString();

		sleRippleState->setFlag(uFlags);
		sleRippleState->setIFieldAmount(sfBalance, saZero);	// Zero balance in currency.
		sleRippleState->setIFieldAmount(bFlipped ? sfHighLimit : sfLowLimit, saLimitAmount);
		sleRippleState->setIFieldAmount(bFlipped ? sfLowLimit : sfHighLimit, saZero);
		sleRippleState->setIFieldAccount(bFlipped ? sfHighID : sfLowID, uSrcAccountID);
		sleRippleState->setIFieldAccount(bFlipped ? sfLowID : sfHighID, uDstAccountID);
	}

	if (bAddIndex)
	{
		uint64			uSrcRef;	// Ignored, ripple_state dirs never delete.

		// XXX Make dirAdd more flexiable to take vector.
		terResult	= dirAdd(uSrcRef, Ledger::getRippleDirIndex(uSrcAccountID), sleRippleState->getIndex());
	}

	Log(lsINFO) << "doCreditSet<";

	return terResult;
}

TransactionEngineResult TransactionEngine::doNicknameSet(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID)
{
	std::cerr << "doNicknameSet>" << std::endl;

	uint256				uNickname		= txn.getITFieldH256(sfNickname);
	bool				bMinOffer		= txn.getITFieldPresent(sfMinimumOffer);
	STAmount			saMinOffer		= bMinOffer ? txn.getITFieldAmount(sfAmount) : STAmount();

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleNickname		= mLedger->getNickname(qry, uNickname);

	if (sleNickname)
	{
		// Edit old entry.
		sleNickname->setIFieldAccount(sfAccount, uSrcAccountID);

		if (bMinOffer && !saMinOffer.isZero())
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

		sleNickname->setIFieldAccount(sfAccount, uSrcAccountID);

		if (bMinOffer && !saMinOffer.isZero())
			sleNickname->setIFieldAmount(sfMinimumOffer, saMinOffer);
	}

	std::cerr << "doNicknameSet<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordFund(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID)
{
	std::cerr << "doPasswordFund>" << std::endl;

	uint160				uDstAccountID	= txn.getITFieldAccount(sfDestination);
	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleDst			= uSrcAccountID == uDstAccountID
											? sleSrc
											: mLedger->getAccountRoot(qry, uDstAccountID);
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

		if (uSrcAccountID != uDstAccountID) {
			std::cerr << "doPasswordFund: Destination modified." << std::endl;

			entryModify(sleDst);
		}
	}

	std::cerr << "doPasswordFund<" << std::endl;

	return terSUCCESS;
}

TransactionEngineResult TransactionEngine::doPasswordSet(const SerializedTransaction& txn, SLE::pointer sleSrc)
{
	std::cerr << "doPasswordSet>" << std::endl;

	if (sleSrc->getFlags() & lsfPasswordSpent)
	{
		std::cerr << "doPasswordSet: Delay transaction: Funds already spent." << std::endl;

		return terFUNDS_SPENT;
	}

	sleSrc->setFlag(lsfPasswordSpent);

	TransactionEngineResult	terResult	= setAuthorized(txn, sleSrc, false);

	std::cerr << "doPasswordSet<" << std::endl;

	return terResult;
}

#ifdef WORK_IN_PROGRESS
TransactionEngineResult calcOfferFill(SAAmount& saSrc, paymentNode& pnSrc, paymentNode& pnDst)
{
	TransactionEngineResult	terResult;

	if (!saSrc.isZero())
	{

	}

	return bSuccess;
}

// Find offers to satisfy pnDst.
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

	terResult	= calcOfferFill(pnSrc.saIOURedeem, pnSrc, pnDst, bAllowPartial);

	if (terSUCCESS == terResult)
	{
		terResult	= calcOfferFill(pnSrc.saIOUIssue, pnSrc, pnDst, bAllowPartial)
	}

	if (terSUCCESS == terResult && !bAllowPartial)
	{
		STAmount	saTotal	= pnSrc.saIOURedeem;
		saTotal	+= pnSrc.saIOUIssue;

		if (saTotal != saWanted)
			terResult	= terINSUF_PATH;
	}

	return terResult;
}

// From the destination work towards the source calculating how much must be asked for.
// --> bAllowPartial: If false, can fail if can't meet requirements.
// <-- bSuccess: true=success, false=insufficient funds.
// <-> pnNodes:
//     --> [end]saWanted.mAmount
//     --> [all]saWanted.mCurrency
//     --> [all]saAccount
//     <-> [0]saWanted.mAmount : --> limit, <-- actual
bool calcPaymentReverse(std::vector<paymentNode>& pnNodes, bool bAllowPartial)
{
	bool	bDone		= false;
	bool	bSuccess	= false;

	// path: dst .. src

	while (!bDone)
	{
		if (cur->saWanted.isZero())
		{
			// Must want something.
			terResult	= terINVALID;
			bDone		= true;
		}
		else if (cur->saWanted.isNative())
		{
			if (prv->how == direct)
			{
				// Stamp transfer desired.
				if (prv->prev())
				{
					// More entries before stamp transfer.
					terResult	= terINVALID;
					bDone		= true;
				}
				else if (prv->account->saBalance() >= cur->saWanted)
				{
					// Transfer stamps.
					prv->saSend = cur->saWanted;
					bDone		= true;
					bSuccess	= true;
				}
				else
				{
					// Insufficient funds for transfer
					bDone		= true;
				}
			}
			else
			{
				// Must convert to stamps via offer.
				if (calcOfferFill(prv, cur, bAllowPartial))
				{

				}
				else
				{
					bDone	= false;
				}
			}
		}
		else
		{
			// Rippling.

		}
	}
}

// From the source work toward the destination calculate how much is transfered at each step and finally.
// <-> pnNodes:
//     --> [0]saWanted.mAmount
//     --> [all]saWanted.saSend
//     --> [all]saWanted.IOURedeem
//     --> [all]saWanted.IOUIssue
//     --> [all]saAccount
bool calcPaymentForward(std::vector<paymentNode>& pnNodes)
{
	cur = src;

	if (!cur->saSend.isZero())
	{
		// Sending stamps - always final step.
		assert(!cur->next);
		nxt->saReceive	= cur->saSend;
		bDone			= true;
	}
	else
	{
		// Rippling.

	}
}
#endif

// XXX Need to audit for things like setting accountID not having memory.
TransactionEngineResult TransactionEngine::doPayment(const SerializedTransaction& txn,
	SLE::pointer sleSrc,
	const uint160& uSrcAccountID)
{
	// Ripple if source or destination is non-native or if there are paths.
	uint32		txFlags			= txn.getFlags();
	bool		bCreate			= !!(txFlags & tfCreateAccount);
	bool		bNoRippleDirect	= !!(txFlags & tfNoRippleDirect);
	bool		bPaths			= txn.getITFieldPresent(sfPaths);
	bool		bMax			= txn.getITFieldPresent(sfSendMax);
	uint160		uDstAccountID	= txn.getITFieldAccount(sfDestination);
	STAmount	saDstAmount		= txn.getITFieldAmount(sfAmount);
	STAmount	saMaxAmount		= bMax ? txn.getITFieldAmount(sfSendMax) : saDstAmount;
	uint160		uSrcCurrency	= saMaxAmount.getCurrency();
	uint160		uDstCurrency	= saDstAmount.getCurrency();

	if (!uDstAccountID)
	{
		Log(lsINFO) << "doPayment: Invalid transaction: Payment destination account not specifed.";

		return tenDST_NEEDED;
	}
	else if (!saDstAmount.isPositive())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: bad amount: " << saDstAmount.getCurrencyHuman() << " " << saDstAmount.getText();

		return tenBAD_AMOUNT;
	}
	else if (uSrcAccountID == uDstAccountID && uSrcCurrency == uDstCurrency && !bPaths)
	{
		Log(lsINFO) << boost::str(boost::format("doPayment: Invalid transaction: Redunant transaction: src=%s, dst=%s, src_cur=%s, dst_cur=%s")
			% uSrcAccountID.ToString()
			% uDstAccountID.ToString()
			% uSrcCurrency.ToString()
			% uDstCurrency.ToString());

		return tenREDUNDANT;
	}

	LedgerStateParms	qry		= lepNONE;
	SLE::pointer		sleDst	= mLedger->getAccountRoot(qry, uDstAccountID);
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

		sleDst->setIFieldAccount(sfAccount, uDstAccountID);
		sleDst->setIFieldU32(sfSequence, 1);
	}
	// Destination exists.
	else if (bCreate)
	{
		// Retryable: if account created this ledger, reordering might allow account to be made by this transaction.
		Log(lsINFO) << "doPayment: Invalid transaction: Account already created.";

		return terCREATED;
	}
	else
	{
		entryModify(sleDst);
	}

	bool		bRipple			= bPaths || bMax || !saDstAmount.isNative();

	if (!bRipple)
	{
		// Direct XNS payment.
		STAmount	saSrcXNSBalance	= sleSrc->getIValueFieldAmount(sfBalance);

		if (saSrcXNSBalance < saDstAmount)
		{
			// Transaction might succeed, if applied in a different order.
			Log(lsINFO) << "doPayment: Delay transaction: Insufficent funds.";

			return terUNFUNDED;
		}

		sleSrc->setIFieldAmount(sfBalance, saSrcXNSBalance - saDstAmount);
		sleDst->setIFieldAmount(sfBalance, sleDst->getIValueFieldAmount(sfBalance) + saDstAmount);

		return terSUCCESS;
	}

	//
	// Ripple payment
	//
	// XXX Disallow loops in ripple paths

	// Try direct ripple first.
	if (!bNoRippleDirect && uSrcAccountID != uDstAccountID && uSrcCurrency == uDstCurrency)
	{
							qry				= lepNONE;
		SLE::pointer		sleRippleState	= mLedger->getRippleState(qry, uSrcAccountID, uDstAccountID, uDstCurrency);

		if (sleRippleState)
		{
			// There is a direct credit-line of some direction.
			// - We can always pay IOUs back.
			// - We can issue IOUs to the limit.
			// XXX Not implemented:
			// - Give preference to pushing out IOUs over sender credit limit.
			// - Give preference to pushing out IOUs to creating them.
			// - Create IOUs as last resort.
			uint160		uLowID			= sleRippleState->getIValueFieldAccount(sfLowID).getAccountID();
			uint160		uHighID			= sleRippleState->getIValueFieldAccount(sfHighID).getAccountID();
			bool		bSendHigh		= uLowID == uSrcAccountID && uHighID == uDstAccountID;
			bool		bSendLow		= uLowID == uDstAccountID && uHighID == uSrcAccountID;
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
					Ledger::getRippleDirIndex(uSrcAccountID),		// The source ended up owing.
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

	STPathSet	spsPaths = txn.getITFieldPathSet(sfPaths);

	// XXX If we are parsing for determing forwarding check maximum path count.
	if (!spsPaths.getPathCount())
	{
		Log(lsINFO) << "doPayment: Invalid transaction: No paths.";

		return tenRIPPLE_EMPTY;
	}
#if 0
	std::vector<STPath> spPath;

	BOOST_FOREACH(std::vector<STPath>& spPath, spsPaths)
	{

		Log(lsINFO) << "doPayment: Implementation error: Not implemented.";

		return tenUNKNOWN;
	}
#endif

	Log(lsINFO) << "doPayment: Delay transaction: No ripple paths could be satisfied.";

	return terBAD_RIPPLE;
}

TransactionEngineResult TransactionEngine::doWalletAdd(const SerializedTransaction& txn, SLE::pointer sleSrc)
{
	std::cerr << "WalletAdd>" << std::endl;

	std::vector<unsigned char>	vucPubKey		= txn.getITFieldVL(sfPubKey);
	std::vector<unsigned char>	vucSignature	= txn.getITFieldVL(sfSignature);
	uint160						uAuthKeyID		= txn.getITFieldAccount(sfAuthorizedKey);
	NewcoinAddress				naMasterPubKey	= NewcoinAddress::createAccountPublic(vucPubKey);
	uint160						uDstAccountID	= naMasterPubKey.getAccountID();

	if (!naMasterPubKey.accountPublicVerify(Serializer::getSHA512Half(uAuthKeyID.begin(), uAuthKeyID.size()), vucSignature))
	{
		std::cerr << "WalletAdd: unauthorized:  bad signature " << std::endl;

		return tenBAD_ADD_AUTH;
	}

	LedgerStateParms	qry				= lepNONE;
	SLE::pointer		sleDst			= mLedger->getAccountRoot(qry, uDstAccountID);

	if (sleDst)
	{
		std::cerr << "WalletAdd: account already created" << std::endl;

		return tenCREATED;
	}

	STAmount			saAmount		= txn.getITFieldAmount(sfAmount);
	STAmount			saSrcBalance	= sleSrc->getIValueFieldAmount(sfBalance);

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
	sleSrc->setIFieldAmount(sfBalance, saSrcBalance-saAmount);

	// Create the account.
	sleDst	= entryCreate(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(uDstAccountID));

	sleDst->setIFieldAccount(sfAccount, uDstAccountID);
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

// Before an offer is place into the ledger, fill as much as possible.
// -->   uBookBase: The order book to take against.
// --> saTakerPays: What the taker wanted (w/ issuer)
// --> saTakerGets: What the taker wanted (w/ issuer)
// --> saTakerFund: What taker can afford
// <-- saTakerPaid: What taker actually paid
// <--  saTakerGot: What taker actually got
TransactionEngineResult TransactionEngine::takeOffers(
	bool			bPassive,
	const uint256&	uBookBase,
	const uint160&	uTakerAccountID,
	const STAmount&	saTakerPays,
	const STAmount&	saTakerGets,
	const STAmount&	saTakerFunds,
	STAmount&		saTakerPaid,
	STAmount&		saTakerGot)
{
	assert(!saTakerPays.isZero() && !saTakerGets.isZero());

	uint256					uTipIndex			= uBookBase;
	uint256					uBookEnd			= Ledger::getQualityNext(uBookBase);
	uint64					uTakeQuality		= STAmount::getRate(saTakerGets, saTakerPays);
	uint160					uTakerPaysAccountID	= saTakerPays.getIssuer();
	uint160					uTakerPaysCurrency	= saTakerPays.getCurrency();
	uint160					uTakerGetsAccountID	= saTakerGets.getIssuer();
	uint160					uTakerGetsCurrency	= saTakerGets.getCurrency();
	TransactionEngineResult	terResult			= tenUNKNOWN;

	saTakerPaid	= 0;
	saTakerGot	= 0;

	while (tenUNKNOWN == terResult)
	{
		SLE::pointer	sleOffer;
		uint64			uTipQuality;

		if (saTakerGets != saTakerGot && saTakerPays != saTakerPaid)
		{
			// Taker has needs.
			sleOffer		= mLedger->getNextSLE(uTipIndex, uBookEnd);
			if (sleOffer)
			{
				uTipIndex		= sleOffer->getIndex();
				uTipQuality		= Ledger::getQuality(uTipIndex);
			}
		}

		if (!sleOffer || uTakeQuality < uTipQuality || (bPassive && uTakeQuality == uTipQuality))
		{
			// Done.
			terResult	= terSUCCESS;
		}
		else
		{
			// Have an offer to consider.
			NewcoinAddress	naOfferAccountID	= sleOffer->getIValueFieldAccount(sfAccount);
			STAmount		saOfferPays			= sleOffer->getIValueFieldAmount(sfTakerGets);
			STAmount		saOfferGets			= sleOffer->getIValueFieldAmount(sfTakerPays);
			uint160			uOfferAccountID		= naOfferAccountID.getAccountID();

			if (uOfferAccountID == uTakerAccountID)
			{
				// Would take own offer. Consider it unfunded.

				entryUnfunded(sleOffer);
			}
			else if (sleOffer->getIFieldPresent(sfExpiration) && sleOffer->getIFieldU32(sfExpiration) <= mLedger->getParentCloseTimeNC())
			{
				// Offer is expired. Delete it.

				entryUnfunded(sleOffer);
			}
			else
			{
				// Get offer funds available.
				STAmount			saOfferFunds;
				SLE::pointer		sleOfferAccount;
				SLE::pointer		sleOfferFunds;		// ledger entry of funding

				if (saTakerGets.isNative())
				{
					// Handle getting stamps.

					LedgerStateParms	qry				= lepNONE;

					sleOfferAccount	= mLedger->getAccountRoot(qry, naOfferAccountID);
					if (!sleOfferAccount)
					{
						Log(lsWARNING) << "takeOffers: delay: can't receive stamps from non-existant account";

						terResult	= terNO_ACCOUNT;
					}
					else
					{
						saOfferFunds	= sleOfferAccount->getIValueFieldAmount(sfBalance);
						sleOfferFunds	= sleOfferAccount;
					}
				}
				else
				{
					// Handling getting ripple.

					if (saTakerGets.getIssuer() == uOfferAccountID)
					{
						// Taker gets offer's IOUs from offerer: fully funded.

						saOfferFunds	= saOfferPays;
					}
					else
					{
						// offerer's line of credit with offerer pay's issuer
						saOfferFunds	= rippleBalance(uOfferAccountID, uTakerGetsAccountID, uTakerGetsCurrency);
						sleOfferFunds	= sleOfferAccount;
					}
				}

				if (tenUNKNOWN != terResult)
				{
					nothing();
				}
				else if (saOfferFunds.isPositive())
				{
					STAmount	saSubTakerPaid;
					STAmount	saSubTakerGot;

					bool	bOfferDelete	= STAmount::applyOffer(
						saOfferFunds, saTakerFunds,
						saOfferPays, saOfferGets,
						saTakerPays, saTakerGets,
						saSubTakerPaid, saSubTakerGot);

					// Adjust offer
					if (bOfferDelete)
					{
						// Offer now fully claimed or now unfunded.

						entryDelete(sleOffer);
					}
					else
					{
						sleOffer->setIFieldAmount(sfTakerGets, saOfferPays -= saSubTakerGot);
						sleOffer->setIFieldAmount(sfTakerPays, saOfferGets += saSubTakerPaid);

						entryModify(sleOffer);
					}

					// Pay taker (debit offer issuer)
					if (saTakerGets.isNative())
					{
						sleOfferAccount->setIFieldAmount(sfBalance,
							sleOfferAccount->getIValueFieldAmount(sfBalance) - saSubTakerGot);

						entryModify(sleOfferAccount);
					}
					else
					{
						rippleDebit(uOfferAccountID, uTakerGetsAccountID, uTakerGetsCurrency, saSubTakerGot);
					}
					saTakerGot	+= saSubTakerGot;

					// Pay offer
					if (saTakerPays.isNative())
					{
						sleOfferAccount->setIFieldAmount(sfBalance,
							sleOfferAccount->getIValueFieldAmount(sfBalance) + saSubTakerPaid);

						entryModify(sleOfferAccount);
					}
					else
					{
						rippleCredit(uOfferAccountID, uTakerPaysAccountID, uTakerPaysCurrency, saSubTakerPaid);
					}
					saTakerPaid	+= saSubTakerPaid;
				}
				else if (entryExists(sleOfferFunds))
				{
					// Offer is unfunded, possibly due to previous balance action.

					entryDelete(sleOffer);		// Only delete, if transaction succeeds.
				}
				else
				{
					// Offer is unfunded, outright.

					entryUnfunded(sleOffer);	// Always delete as was originally unfunded.
				}
			}
		}
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCreate(const SerializedTransaction& txn, SLE::pointer sleSrc, const uint160& uSrcAccountID)
{
	uint32					txFlags			= txn.getFlags();
	bool					bPassive		= !!(txFlags & tfPassive);
	STAmount				saTakerPays		= txn.getITFieldAmount(sfTakerPays);
	STAmount				saTakerGets		= txn.getITFieldAmount(sfTakerGets);
	uint160					uPaysIssuerID	= txn.getITFieldAccount(sfPaysIssuer);
	uint160					uGetsIssuerID	= txn.getITFieldAccount(sfGetsIssuer);
	uint32					uExpiration		= txn.getITFieldU32(sfExpiration);
	bool					bHaveExpiration	= txn.getITFieldPresent(sfExpiration);
	uint32					uSequence		= txn.getSequence();

	// LedgerStateParms		qry				= lepNONE;
	uint256					uLedgerIndex	= Ledger::getOfferIndex(uSrcAccountID, uSequence);
	SLE::pointer			sleOffer		= entryCreate(ltOFFER, uLedgerIndex);

	Log(lsINFO) << "doOfferCreate: Creating offer node: " << uLedgerIndex.ToString() << " uSequence=" << uSequence;

	uint160					uPaysCurrency	= saTakerPays.getCurrency();
	uint160					uGetsCurrency	= saTakerGets.getCurrency();
	uint64					uRate			= STAmount::getRate(saTakerGets, saTakerPays);

	TransactionEngineResult	terResult		= terSUCCESS;
	STAmount				saOfferFunds;
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
	else if (!saTakerGets.isNative() && uGetsIssuerID == uSrcAccountID)
	{
		// Delivering self issued IOUs.

		saOfferFunds	= saTakerGets;
	}
	else
	{
		// Make sure signer has funds.
		saOfferFunds	= saTakerGets.isNative()
							? sleSrc->getIValueFieldAmount(sfBalance)
							: rippleBalance(uSrcAccountID, uGetsIssuerID, uGetsCurrency);

		Log(lsWARNING) << "doOfferCreate: takeOffers: saTakerGets.isNative()=" << saTakerGets.isNative();
		Log(lsWARNING) << "doOfferCreate: takeOffers: saOfferFunds=" << saOfferFunds.getText();

		if (!saOfferFunds.isPositive())
		{
			Log(lsWARNING) << "doOfferCreate: delay: offers must be funded";

			terResult	= terUNFUNDED;
		}
	}

	Log(lsWARNING) << "doOfferCreate: takeOffers: saOfferFunds=" << saOfferFunds.getText();

	if (terSUCCESS == terResult && !saTakerPays.isNative())
	{
		LedgerStateParms	qry				= lepNONE;
		SLE::pointer		sleTakerPays	= mLedger->getAccountRoot(qry, uPaysIssuerID);

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

		// Take using the parameters of the offer.
		terResult	= takeOffers(
						bPassive,
						Ledger::getBookBase(uGetsCurrency, uGetsIssuerID, uPaysCurrency, uPaysIssuerID),
						uSrcAccountID,
						saTakerGets,
						saTakerPays,
						saOfferFunds,	// Limit to spend.
						saOfferPaid,	// How much was spent.
						saOfferGot		// How much was got.
					);

		Log(lsWARNING) << "doOfferCreate: takeOffers=" << terResult;

		if (terSUCCESS == terResult)
		{
			saOfferFunds	-= saOfferPaid;				// Reduce balance.

			saTakerGets		-= saOfferPaid;				// Reduce payout to takers by what srcAccount just paid.
			saTakerPays		-= saOfferGot;				// Reduce payin from takers by what offer just got.

			// Adjust funding source.
			if (saOfferPaid.isZero())
			{
				Log(lsWARNING) << "doOfferCreate: takeOffers: none paid.";

				nothing();
			}
			else if (!saTakerGets.isNative() && uGetsIssuerID == uSrcAccountID)
			{
				// Delivering self-issued IOUs.
				nothing();
			}
			else if (saTakerGets.isNative())
			{
				sleSrc->setIFieldAmount(sfBalance, saOfferFunds - saOfferPaid);
			}
			else
			{
				rippleDebit(uSrcAccountID, uGetsIssuerID, uGetsCurrency, saOfferPaid);
			}

			// Adjust payout target.
			if (saOfferGot.isZero())
			{
				Log(lsWARNING) << "doOfferCreate: takeOffers: none got.";

				nothing();
			}
			else if (!saTakerPays.isNative() && uPaysIssuerID == uSrcAccountID)
			{
				// Destroying self-issued IOUs.
				nothing();
			}
			else if (saTakerGets.isNative())
			{
				sleSrc->setIFieldAmount(sfBalance, sleSrc->getIValueFieldAmount(sfBalance) - saOfferGot);
			}
			else
			{
				rippleCredit(uSrcAccountID, uPaysIssuerID, uPaysCurrency, saOfferGot);
			}
		}
	}

	// Log(lsWARNING) << "doOfferCreate: takeOffers:  saOfferFunds=" << saOfferFunds.getText();
	// Log(lsWARNING) << "doOfferCreate: takeOffers:   saTakerPays=" << saTakerPays.getText();
	// Log(lsWARNING) << "doOfferCreate: takeOffers:   saTakerGets=" << saTakerGets.getText();
	// Log(lsWARNING) << "doOfferCreate: takeOffers: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
	// Log(lsWARNING) << "doOfferCreate: takeOffers: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);

	if (terSUCCESS == terResult
		&& !saOfferFunds.isZero()						// Still funded.
		&& !saTakerGets.isZero()						// Still offering something.
		&& !saTakerPays.isZero())						// Still wanting something.
	{
		// We need to place the remainder of the offer into its order book.

		// Add offer to owner's directory.
		terResult	= dirAdd(uOwnerNode, Ledger::getOwnerDirIndex(uSrcAccountID), uLedgerIndex);

		if (terSUCCESS == terResult)
		{
			uDirectory	= Ledger::getQualityIndex(
								Ledger::getBookBase(uPaysCurrency, uPaysIssuerID, uGetsCurrency, uGetsIssuerID),
								uRate);					// Use original rate.

			// Add offer to order book.
			terResult	= dirAdd(uBookNode, uDirectory, uLedgerIndex);
		}

		if (terSUCCESS == terResult)
		{
			// Log(lsWARNING) << "doOfferCreate: uPaysIssuerID=" << NewcoinAddress::createHumanAccountID(uPaysIssuerID);
			// Log(lsWARNING) << "doOfferCreate: uGetsIssuerID=" << NewcoinAddress::createHumanAccountID(uGetsIssuerID);
			// Log(lsWARNING) << "doOfferCreate: saTakerPays.isNative()=" << saTakerPays.isNative();
			// Log(lsWARNING) << "doOfferCreate: saTakerGets.isNative()=" << saTakerGets.isNative();
			// Log(lsWARNING) << "doOfferCreate: uPaysCurrency=" << saTakerPays.getCurrencyHuman();
			// Log(lsWARNING) << "doOfferCreate: uGetsCurrency=" << saTakerGets.getCurrencyHuman();

			sleOffer->setIFieldAccount(sfAccount, uSrcAccountID);
			sleOffer->setIFieldU32(sfSequence, uSequence);
			sleOffer->setIFieldH256(sfDirectory, uDirectory);
			sleOffer->setIFieldAmount(sfTakerPays, saTakerPays);
			sleOffer->setIFieldAmount(sfTakerGets, saTakerGets);
			sleOffer->setIFieldU64(sfOwnerNode, uOwnerNode);
			sleOffer->setIFieldU64(sfBookNode, uBookNode);

			if (!saTakerPays.isNative())
				sleOffer->setIFieldAccount(sfPaysIssuer, uPaysIssuerID);

			if (!saTakerGets.isNative())
				sleOffer->setIFieldAccount(sfGetsIssuer, uGetsIssuerID);

			sleOffer->setIFieldU32(sfExpiration, uExpiration);

			if (bPassive)
				sleOffer->setFlag(lsfPassive);
		}
	}

	return terResult;
}

TransactionEngineResult TransactionEngine::doOfferCancel(const SerializedTransaction& txn, const uint160& uSrcAccountID)
{
	uint32					uSequence		= txn.getITFieldU32(sfOfferSequence);
	uint256					uLedgerIndex	= Ledger::getOfferIndex(uSrcAccountID, uSequence);

	LedgerStateParms		qry				= lepNONE;
	SLE::pointer			sleOffer		= mLedger->getOffer(qry, uLedgerIndex);
	TransactionEngineResult	terResult;

	if (sleOffer)
	{
		Log(lsWARNING) << "doOfferCancel: uSequence=" << uSequence;

		uint64		uOwnerNode	= sleOffer->getIFieldU64(sfOwnerNode);

		terResult	= dirDelete(true, uOwnerNode, Ledger::getOwnerDirIndex(uSrcAccountID), uLedgerIndex);

		if (terSUCCESS == terResult)
		{
			uint256		uDirectory	= sleOffer->getIFieldH256(sfDirectory);
			uint64		uBookNode	= sleOffer->getIFieldU64(sfBookNode);

			terResult	= dirDelete(false, uBookNode, uDirectory, uLedgerIndex);
		}

		entryDelete(sleOffer);
	}
	else
	{
		Log(lsWARNING) << "doOfferCancel: offer not found: "
			<< NewcoinAddress::createHumanAccountID(uSrcAccountID)
			<< " : " << uSequence
			<< " : " << uLedgerIndex.ToString();

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
