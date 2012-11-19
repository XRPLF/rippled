//
// XXX Make sure all fields are recognized in transactions.
//

#include <boost/format.hpp>
#include <boost/foreach.hpp>
#include <boost/smart_ptr/shared_ptr.hpp>

#include "TransactionEngine.h"
#include "Transactor.h"

#include "../json/writer.h"

#include "Config.h"
#include "Log.h"
#include "TransactionFormats.h"
#include "utils.h"

SETUP_LOG();
DECLARE_INSTANCE(TransactionEngine);

void TransactionEngine::txnWrite()
{
	// Write back the account states
	typedef std::pair<const uint256, LedgerEntrySetEntry> u256_LES_pair;
	BOOST_FOREACH(u256_LES_pair& it, mNodes)
	{
		const SLE::pointer&	sleEntry	= it.second.mEntry;

		switch (it.second.mAction)
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

					if (!mLedger->peekAccountStateMap()->delItem(it.first))
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

	Transactor::pointer transactor=Transactor::makeTransactor(txn,params,this);
	if(transactor)
	{
		uint256 txID		= txn.getTransactionID();
		if (!txID)
		{
			cLog(lsWARNING) << "applyTransaction: invalid transaction id";

			return temINVALID;
		}

		TER terResult= transactor->apply();
		std::string	strToken;
		std::string	strHuman;

		transResultInfo(terResult, strToken, strHuman);

		cLog(lsINFO) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

		if (isTepPartial(terResult) && isSetBit(params, tapRETRY))
		{
			// Partial result and allowed to retry, reclassify as a retry.
			terResult	= terRETRY;
		}

		if ((tesSUCCESS == terResult) || isTepPartial(terResult))
		{
			// Transaction succeeded fully or (retries are not allowed and the transaction succeeded partially).
			Serializer m;
			mNodes.calcRawMeta(m, terResult);

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

				STAmount saPaid = txn.getTransactionFee();
				// Charge whatever fee they specified.
				mLedger->destroyCoins(saPaid.getNValue());
			}
		}

		mTxnAccount.reset();
		mNodes.clear();

		if (!isSetBit(params, tapOPEN_LEDGER)
			&& (isTemMalformed(terResult) || isTefFailure(terResult)))
		{
			// XXX Malformed or failed transaction in closed ledger must bow out.
		}

		return terResult;
	}else 
	{
		cLog(lsWARNING) << "applyTransaction: Invalid transaction: unknown transaction type";
		return temUNKNOWN;
	}
}

// vim:ts=4

