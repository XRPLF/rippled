//
// XXX Make sure all fields are recognized in transactions.
//

#include <boost/format.hpp>

#include "TransactionEngine.h"

#include "../json/writer.h"

#include "Config.h"
#include "Log.h"
#include "TransactionFormats.h"
#include "utils.h"

SETUP_LOG();

void TransactionEngine::txnWrite()
{
	// Write back the account states
	for (std::map<uint256, LedgerEntrySetEntry>::iterator it = mNodes.begin(), end = mNodes.end();
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
					cLog(lsINFO) << "applyTransaction: taaCREATE: " << sleEntry->getText();

					if (mLedger->writeBack(lepCREATE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaMODIFY:
				{
					cLog(lsINFO) << "applyTransaction: taaMODIFY: " << sleEntry->getText();

					if (mLedger->writeBack(lepNONE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaDELETE:
				{
					cLog(lsINFO) << "applyTransaction: taaDELETE: " << sleEntry->getText();

					if (!mLedger->peekAccountStateMap()->delItem(it->first))
						assert(false);
				}
				break;
		}
	}
}

TER TransactionEngine::applyTransaction(const SerializedTransaction& txn, TransactionEngineParams params)
{
	cLog(lsTRACE) << "applyTransaction>";
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
			cLog(lsFATAL) << "Transaction serdes mismatch";
			Json::StyledStreamWriter ssw;
			cLog(lsINFO) << txn.getJson(0);
			cLog(lsFATAL) << s2.getJson(0);
			assert(false);
		}
	}
#endif

	TER		terResult	= tesSUCCESS;
	uint256 txID		= txn.getTransactionID();
	if (!txID)
	{
		cLog(lsWARNING) << "applyTransaction: invalid transaction id";

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
		naSigningPubKey	= NewcoinAddress::createAccountPublic(txn.getSigningPubKey());

	// Consistency: really signed.
	if ((tesSUCCESS == terResult) && !isSetBit(params, tapNO_CHECK_SIGN) && !txn.checkSign(naSigningPubKey))
	{
		cLog(lsWARNING) << "applyTransaction: Invalid transaction: bad signature";

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
					SLE::pointer		sleNickname		= entryCache(ltNICKNAME, txn.getFieldH256(sfNickname));

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
				cLog(lsWARNING) << "applyTransaction: Invalid transaction: ttINVALID transaction type";
				terResult = temINVALID;
				break;

			default:
				cLog(lsWARNING) << "applyTransaction: Invalid transaction: unknown transaction type";
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
				cLog(lsINFO) << "applyTransaction: insufficient fee";

				terResult	= telINSUF_FEE_P;
			}
		}
		else
		{
			if (saPaid)
			{
				// Transaction is malformed.
				cLog(lsWARNING) << "applyTransaction: fee not allowed";

				terResult	= temINSUF_FEE_P;
			}
		}
	}

	// Get source account ID.
	mTxnAccountID	= txn.getSourceAccount().getAccountID();
	if (tesSUCCESS == terResult && !mTxnAccountID)
	{
		cLog(lsWARNING) << "applyTransaction: bad source id";

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
		cLog(lsTRACE) << boost::str(boost::format("applyTransaction: Delay transaction: source account does not exist: %s") %
			txn.getSourceAccount().humanAccountID());

		terResult			= terNO_ACCOUNT;
	}
	else
	{
		saSrcBalance	= mTxnAccount->getFieldAmount(sfBalance);
		bHaveAuthKey	= mTxnAccount->isFieldPresent(sfAuthorizedKey);
	}

	// Check if account claimed.
	if (tesSUCCESS == terResult)
	{
		switch (txn.getTxnType())
		{
			case ttCLAIM:
				if (bHaveAuthKey)
				{
					cLog(lsWARNING) << "applyTransaction: Account already claimed.";

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
					cLog(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					cLog(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= tefBAD_CLAIM_ID;
				}
				break;

			case ttPASSWORD_SET:
				// Transaction's signing public key must be for the source account.
				// To prove the master private key made this transaction.
				if (naSigningPubKey.getAccountID() != mTxnAccountID)
				{
					// Signing Pub Key must be for Source Account ID.
					cLog(lsWARNING) << "sourceAccountID: " << naSigningPubKey.humanAccountID();
					cLog(lsWARNING) << "txn accountID: " << txn.getSourceAccount().humanAccountID();

					terResult	= temBAD_SET_ID;
				}
				break;

			default:
				// Verify the transaction's signing public key is the key authorized for signing.
				if (bHaveAuthKey && naSigningPubKey.getAccountID() == mTxnAccount->getFieldAccount(sfAuthorizedKey).getAccountID())
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
					cLog(lsINFO) << "applyTransaction: Delay: Not authorized to use account.";

					terResult	= tefBAD_AUTH;
				}
				else
				{
					cLog(lsINFO) << "applyTransaction: Invalid: Not authorized to use account.";

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
		cLog(lsINFO)
			<< boost::str(boost::format("applyTransaction: Delay: insufficient balance: balance=%s paid=%s")
				% saSrcBalance.getText()
				% saPaid.getText());

		terResult	= terINSUF_FEE_B;
	}
	else
	{
		mTxnAccount->setFieldAmount(sfBalance, saSrcBalance - saPaid);
	}

	// Validate sequence
	if (tesSUCCESS != terResult)
	{
		nothing();
	}
	else if (saCost)
	{
		uint32 a_seq = mTxnAccount->getFieldU32(sfSequence);

		cLog(lsTRACE) << "Aseq=" << a_seq << ", Tseq=" << t_seq;

		if (t_seq != a_seq)
		{
			if (a_seq < t_seq)
			{
				cLog(lsINFO) << "applyTransaction: future sequence number";

				terResult	= terPRE_SEQ;
			}
			else if (mLedger->hasTransaction(txID))
				terResult	= tefALREADY;
			else
			{
				cLog(lsWARNING) << "applyTransaction: past sequence number";

				terResult	= tefPAST_SEQ;
			}
		}
		else
		{
			mTxnAccount->setFieldU32(sfSequence, t_seq + 1);
		}
	}
	else
	{
		cLog(lsINFO) << "applyTransaction: Zero cost transaction";

		if (t_seq)
		{
			cLog(lsINFO) << "applyTransaction: bad sequence for pre-paid transaction";

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
				cLog(lsINFO) << "applyTransaction: invalid type";
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
				terResult = doPayment(txn, params);
				break;

			case ttWALLET_ADD:
				terResult = doWalletAdd(txn);
				break;

			case ttCONTRACT:
				terResult = doContractAdd(txn);
				break;
			case ttCONTRACT_REMOVE:
				terResult = doContractRemove(txn);
				break;

			default:
				terResult = temUNKNOWN;
				break;
		}
	}

	std::string	strToken;
	std::string	strHuman;

	transResultInfo(terResult, strToken, strHuman);

	cLog(lsINFO) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

	if (isTepPartial(terResult) && isSetBit(params, tapRETRY))
	{
		// Partial result and allowed to retry, reclassify as a retry.
		terResult	= terRETRY;
	}

	if (tesSUCCESS == terResult || isTepPartial(terResult))
	{
		// Transaction succeeded fully or (retries are not allowed and the transaction succeeded partially).
		Serializer m;
		mNodes.calcRawMeta(m);

		txnWrite();

		Serializer s;
		txn.add(s);

		if (isSetBit(params, tapOPEN_LEDGER))
		{
			if (!mLedger->addTransaction(txID, s))
				assert(false);
		}
		else
		{
			if (!mLedger->addTransaction(txID, s, m))
				assert(false);

			// Charge whatever fee they specified.
			mLedger->destroyCoins(saPaid.getNValue());
		}
	}

	mTxnAccount	= SLE::pointer();
	mNodes.clear();

	if (!isSetBit(params, tapOPEN_LEDGER)
		&& (isTemMalformed(terResult) || isTefFailure(terResult)))
	{
		// XXX Malformed or failed transaction in closed ledger must bow out.
	}

	return terResult;
}

// vim:ts=4
