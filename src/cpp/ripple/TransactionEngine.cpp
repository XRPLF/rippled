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

DECLARE_INSTANCE(TransactionEngine);

void TransactionEngine::txnWrite()
{
	// Write back the account states
	typedef std::map<uint256, LedgerEntrySetEntry>::value_type u256_LES_pair;
	BOOST_FOREACH(u256_LES_pair& it, mNodes)
	{
		SLE::ref	sleEntry	= it.second.mEntry;

		switch (it.second.mAction)
		{
			case taaNONE:
				assert(false);
				break;

			case taaCACHED:
				break;

			case taaCREATE:
				{
					WriteLog (lsINFO, TransactionEngine) << "applyTransaction: taaCREATE: " << sleEntry->getText();

					if (mLedger->writeBack(lepCREATE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaMODIFY:
				{
					WriteLog (lsINFO, TransactionEngine) << "applyTransaction: taaMODIFY: " << sleEntry->getText();

					if (mLedger->writeBack(lepNONE, sleEntry) & lepERROR)
						assert(false);
				}
				break;

			case taaDELETE:
				{
					WriteLog (lsINFO, TransactionEngine) << "applyTransaction: taaDELETE: " << sleEntry->getText();

					if (!mLedger->peekAccountStateMap()->delItem(it.first))
						assert(false);
				}
				break;
		}
	}
}

TER TransactionEngine::applyTransaction(const SerializedTransaction& txn, TransactionEngineParams params,
	bool& didApply)
{
	WriteLog (lsTRACE, TransactionEngine) << "applyTransaction>";
	didApply = false;
	assert(mLedger);
	mNodes.init(mLedger, txn.getTransactionID(), mLedger->getLedgerSeq(), params);

#ifdef DEBUG
	if (1)
	{
		Serializer ser;
		txn.add(ser);
		SerializerIterator sit(ser);
		SerializedTransaction s2(sit);
		if (!s2.isEquivalent(txn))
		{
			WriteLog (lsFATAL, TransactionEngine) << "Transaction serdes mismatch";
			Json::StyledStreamWriter ssw;
			WriteLog (lsINFO, TransactionEngine) << txn.getJson(0);
			WriteLog (lsFATAL, TransactionEngine) << s2.getJson(0);
			assert(false);
		}
	}
#endif

	UPTR_T<Transactor> transactor = Transactor::makeTransactor(txn, params, this);
	if (transactor.get() != NULL)
	{
		uint256 txID		= txn.getTransactionID();
		if (!txID)
		{
			WriteLog (lsWARNING, TransactionEngine) << "applyTransaction: invalid transaction id";

			return temINVALID;
		}

		TER terResult= transactor->apply();
		std::string	strToken;
		std::string	strHuman;

		transResultInfo(terResult, strToken, strHuman);

		WriteLog (lsINFO, TransactionEngine) << "applyTransaction: terResult=" << strToken << " : " << terResult << " : " << strHuman;

		if (isTesSuccess(terResult))
			didApply = true;
		else if (isTecClaim(terResult) && !isSetBit(params, tapRETRY))
		{ // only claim the transaction fee
			WriteLog (lsDEBUG, TransactionEngine) << "Reprocessing to only claim fee";
			mNodes.clear();

			SLE::pointer txnAcct = entryCache(ltACCOUNT_ROOT, Ledger::getAccountRootIndex(txn.getSourceAccount()));
			if (!txnAcct)
				terResult = terNO_ACCOUNT;
			else
			{
				uint32 t_seq = txn.getSequence();
				uint32 a_seq = txnAcct->getFieldU32(sfSequence);

				if (a_seq < t_seq)
					terResult = terPRE_SEQ;
				else if (a_seq > t_seq)
					terResult = tefPAST_SEQ;
				else
				{
					STAmount fee		= txn.getTransactionFee();
					STAmount balance	= txnAcct->getFieldAmount(sfBalance);

					if (balance < fee)
						terResult = terINSUF_FEE_B;
					else
					{
						txnAcct->setFieldAmount(sfBalance, balance - fee);
						txnAcct->setFieldU32(sfSequence, t_seq + 1);
						entryModify(txnAcct);
						didApply = true;
					}
				}
			}
		}
		else
			WriteLog (lsDEBUG, TransactionEngine) << "Not applying transaction";

		if (didApply)
		{
			if (!checkInvariants(terResult, txn, params))
			{
				WriteLog (lsFATAL, TransactionEngine) << "Transaction violates invariants";
				WriteLog (lsFATAL, TransactionEngine) << txn.getJson(0);
				WriteLog (lsFATAL, TransactionEngine) << transToken(terResult) << ": " << transHuman(terResult);
				WriteLog (lsFATAL, TransactionEngine) << mNodes.getJson(0);
				didApply = false;
				terResult = tefINTERNAL;
			}
			else
			{
				// Transaction succeeded fully or (retries are not allowed and the transaction could claim a fee)
				Serializer m;
				mNodes.calcRawMeta(m, terResult, mTxnSeq++);

				txnWrite();

				Serializer s;
				txn.add(s);

				if (isSetBit(params, tapOPEN_LEDGER))
				{
					if (!mLedger->addTransaction(txID, s))
					{
						WriteLog (lsFATAL, TransactionEngine) << "Tried to add transaction to open ledger that already had it";
						assert(false);
						throw std::runtime_error("Duplicate transaction applied");
					}
				}
				else
				{
					if (!mLedger->addTransaction(txID, s, m))
					{
						WriteLog (lsFATAL, TransactionEngine) << "Tried to add transaction to ledger that already had it";
						assert(false);
						throw std::runtime_error("Duplicate transaction applied to closed ledger");
					}

					// Charge whatever fee they specified.
					STAmount saPaid = txn.getTransactionFee();
					mLedger->destroyCoins(saPaid.getNValue());
				}
			}
		}

		mTxnAccount.reset();
		mNodes.clear();

		if (!isSetBit(params, tapOPEN_LEDGER) && isTemMalformed(terResult))
		{
			// XXX Malformed or failed transaction in closed ledger must bow out.
		}

		return terResult;
	}
	else
	{
		WriteLog (lsWARNING, TransactionEngine) << "applyTransaction: Invalid transaction: unknown transaction type";
		return temUNKNOWN;
	}
}

// vim:ts=4
