
#include <iostream>
#include <fstream>

#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>

#include "../json/writer.h"

#include "Application.h"
#include "Ledger.h"
#include "utils.h"
#include "ripple.pb.h"
#include "PackedMessage.h"
#include "Config.h"
#include "BitcoinUtil.h"
#include "Wallet.h"
#include "LedgerTiming.h"
#include "HashPrefixes.h"
#include "Log.h"

SETUP_LOG();
DECLARE_INSTANCE(Ledger);

Ledger::Ledger(const RippleAddress& masterID, uint64 startAmount) : mTotCoins(startAmount), mLedgerSeq(1),
	mCloseTime(0), mParentCloseTime(0), mCloseResolution(LEDGER_TIME_ACCURACY), mCloseFlags(0),
	mClosed(false), mValidHash(false), mAccepted(false), mImmutable(false),
	mTransactionMap(boost::make_shared<SHAMap>(smtTRANSACTION)),
	mAccountStateMap(boost::make_shared<SHAMap>(smtSTATE))
{
	// special case: put coins in root account
	AccountState::pointer startAccount = boost::make_shared<AccountState>(masterID);
	startAccount->peekSLE().setFieldAmount(sfBalance, startAmount);
	startAccount->peekSLE().setFieldU32(sfSequence, 1);
	cLog(lsTRACE) << "root account: " << startAccount->peekSLE().getJson(0);

	mAccountStateMap->armDirty();
	writeBack(lepCREATE, startAccount->getSLE());
	SHAMap::flushDirty(*mAccountStateMap->disarmDirty(), 256, hotACCOUNT_NODE, mLedgerSeq);
}

Ledger::Ledger(const uint256 &parentHash, const uint256 &transHash, const uint256 &accountHash,
	uint64 totCoins, uint32 closeTime, uint32 parentCloseTime, int closeFlags, int closeResolution, uint32 ledgerSeq)
		: mParentHash(parentHash), mTransHash(transHash), mAccountHash(accountHash), mTotCoins(totCoins),
		mLedgerSeq(ledgerSeq), mCloseTime(closeTime), mParentCloseTime(parentCloseTime),
		mCloseResolution(closeResolution), mCloseFlags(closeFlags),
		mClosed(false), mValidHash(false), mAccepted(false), mImmutable(true),
		mTransactionMap(boost::make_shared<SHAMap>(smtTRANSACTION, transHash)),
		mAccountStateMap(boost::make_shared<SHAMap>(smtSTATE, accountHash))
{ // This will throw if the root nodes are not available locally
	updateHash();
	if (mTransHash.isNonZero())
		mTransactionMap->fetchRoot(mTransHash);
	if (mAccountHash.isNonZero())
		mAccountStateMap->fetchRoot(mAccountHash);
	mTransactionMap->setImmutable();
	mAccountStateMap->setImmutable();
}

Ledger::Ledger(Ledger& ledger, bool isMutable) : mTotCoins(ledger.mTotCoins), mLedgerSeq(ledger.mLedgerSeq),
	mCloseTime(ledger.mCloseTime), mParentCloseTime(ledger.mParentCloseTime),
	mCloseResolution(ledger.mCloseResolution), mCloseFlags(ledger.mCloseFlags),
	mClosed(ledger.mClosed), mValidHash(false), mAccepted(ledger.mAccepted), mImmutable(!isMutable),
	mTransactionMap(ledger.mTransactionMap->snapShot(isMutable)),
	mAccountStateMap(ledger.mAccountStateMap->snapShot(isMutable))
{ // Create a new ledger that's a snapshot of this one
	updateHash();
}


Ledger::Ledger(bool /* dummy */, Ledger& prevLedger) :
	mTotCoins(prevLedger.mTotCoins), mLedgerSeq(prevLedger.mLedgerSeq + 1),
	mParentCloseTime(prevLedger.mCloseTime), mCloseResolution(prevLedger.mCloseResolution),
	mCloseFlags(0),	mClosed(false), mValidHash(false), mAccepted(false), mImmutable(false),
	mTransactionMap(boost::make_shared<SHAMap>(smtTRANSACTION)),
	mAccountStateMap(prevLedger.mAccountStateMap->snapShot(true))
{ // Create a new ledger that follows this one
	prevLedger.updateHash();
	mParentHash = prevLedger.getHash();
	assert(mParentHash.isNonZero());

	mCloseResolution = ContinuousLedgerTiming::getNextLedgerTimeResolution(prevLedger.mCloseResolution,
		prevLedger.getCloseAgree(), mLedgerSeq);
	if (prevLedger.mCloseTime == 0)
	{
		mCloseTime = theApp->getOPs().getCloseTimeNC() - mCloseResolution;
		mCloseTime -= (mCloseTime % mCloseResolution);
	}
	else
		mCloseTime = prevLedger.mCloseTime + mCloseResolution;
}

Ledger::Ledger(const std::vector<unsigned char>& rawLedger) :
	mClosed(false), mValidHash(false), mAccepted(false), mImmutable(true)
{
	Serializer s(rawLedger);
	setRaw(s);
}

Ledger::Ledger(const std::string& rawLedger) :
	mClosed(false), mValidHash(false), mAccepted(false), mImmutable(true)
{
	Serializer s(rawLedger);
	setRaw(s);
}

void Ledger::updateHash()
{
	if (!mImmutable)
	{
		if (mTransactionMap) mTransHash = mTransactionMap->getHash();
		else mTransHash.zero();
		if (mAccountStateMap) mAccountHash = mAccountStateMap->getHash();
		else mAccountHash.zero();
	}

	Serializer s(118);
	s.add32(sHP_Ledger);
	addRaw(s);
	mHash = s.getSHA512Half();
	mValidHash = true;
}

void Ledger::setRaw(Serializer &s)
{
	SerializerIterator sit(s);
	mLedgerSeq =		sit.get32();
	mTotCoins =			sit.get64();
	mParentHash =		sit.get256();
	mTransHash =		sit.get256();
	mAccountHash =		sit.get256();
	mParentCloseTime =	sit.get32();
	mCloseTime =		sit.get32();
	mCloseResolution =	sit.get8();
	mCloseFlags =		sit.get8();
	updateHash();
	if(mValidHash)
	{
		mTransactionMap = boost::make_shared<SHAMap>(smtTRANSACTION, mTransHash);
		mAccountStateMap = boost::make_shared<SHAMap>(smtSTATE, mAccountHash);
	}
}

void Ledger::addRaw(Serializer &s) const
{
	s.add32(mLedgerSeq);
	s.add64(mTotCoins);
	s.add256(mParentHash);
	s.add256(mTransHash);
	s.add256(mAccountHash);
	s.add32(mParentCloseTime);
	s.add32(mCloseTime);
	s.add8(mCloseResolution);
	s.add8(mCloseFlags);
}

void Ledger::setAccepted(uint32 closeTime, int closeResolution, bool correctCloseTime)
{ // used when we witnessed the consensus
	assert(mClosed && !mAccepted);
	mCloseTime = closeTime - (closeTime % closeResolution);
	mCloseResolution = closeResolution;
	mCloseFlags = correctCloseTime ? 0 : sLCF_NoConsensusTime;
	updateHash();
	mAccepted = true;
	mImmutable = true;
}

void Ledger::setAccepted()
{ // used when we acquired the ledger
	// FIXME assert(mClosed && (mCloseTime != 0) && (mCloseResolution != 0));
	mCloseTime -= mCloseTime % mCloseResolution;
	updateHash();
	mAccepted = true;
	mImmutable = true;
}

AccountState::pointer Ledger::getAccountState(const RippleAddress& accountID)
{
#ifdef DEBUG
//	std::cerr << "Ledger:getAccountState(" << accountID.humanAccountID() << ")" << std::endl;
#endif
	ScopedLock l(mAccountStateMap->Lock());
	SHAMapItem::pointer item = mAccountStateMap->peekItem(Ledger::getAccountRootIndex(accountID));
	if (!item)
	{
#ifdef DEBUG
//		std::cerr << " notfound" << std::endl;
#endif
		return AccountState::pointer();
	}
	SerializedLedgerEntry::pointer sle =
		boost::make_shared<SerializedLedgerEntry>(item->peekSerializer(), item->getTag());
	if (sle->getType() != ltACCOUNT_ROOT)
		return AccountState::pointer();
	return boost::make_shared<AccountState>(sle,accountID);
}

NicknameState::pointer Ledger::getNicknameState(const uint256& uNickname)
{
	ScopedLock l(mAccountStateMap->Lock());
	SHAMapItem::pointer item = mAccountStateMap->peekItem(Ledger::getNicknameIndex(uNickname));
	if (!item)
	{
		return NicknameState::pointer();
	}

	SerializedLedgerEntry::pointer sle =
		boost::make_shared<SerializedLedgerEntry>(item->peekSerializer(), item->getTag());
	if (sle->getType() != ltNICKNAME) return NicknameState::pointer();
	return boost::make_shared<NicknameState>(sle);
}

bool Ledger::addTransaction(const uint256& txID, const Serializer& txn)
{ // low-level - just add to table
	SHAMapItem::pointer item = boost::make_shared<SHAMapItem>(txID, txn.peekData());
	if (!mTransactionMap->addGiveItem(item, true, false))
	{
		cLog(lsWARNING) << "Attempt to add transaction to ledger that already had it";
		return false;
	}
	return true;
}

bool Ledger::addTransaction(const uint256& txID, const Serializer& txn, const Serializer& md)
{ // low-level - just add to table
	Serializer s(txn.getDataLength() + md.getDataLength() + 16);
	s.addVL(txn.peekData());
	s.addVL(md.peekData());
	SHAMapItem::pointer item = boost::make_shared<SHAMapItem>(txID, s.peekData());
	if (!mTransactionMap->addGiveItem(item, true, true))
	{
		cLog(lsFATAL) << "Attempt to add transaction+MD to ledger that already had it";
		return false;
	}
	return true;
}

Transaction::pointer Ledger::getTransaction(const uint256& transID) const
{
	SHAMapTreeNode::TNType type;
	SHAMapItem::pointer item = mTransactionMap->peekItem(transID, type);
	if (!item) return Transaction::pointer();

	Transaction::pointer txn = theApp->getMasterTransaction().fetch(transID, false);
	if (txn)
		return txn;

	if (type == SHAMapTreeNode::tnTRANSACTION_NM)
		txn = Transaction::sharedTransaction(item->getData(), true);
	else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
	{
		std::vector<unsigned char> txnData;
		int txnLength;
		if (!item->peekSerializer().getVL(txnData, 0, txnLength))
			return Transaction::pointer();
		txn = Transaction::sharedTransaction(txnData, false);
	}
	else
	{
		assert(false);
		return Transaction::pointer();
	}

	if (txn->getStatus() == NEW)
		txn->setStatus(mClosed ? COMMITTED : INCLUDED, mLedgerSeq);

	theApp->getMasterTransaction().canonicalize(txn, false);
	return txn;
}

SerializedTransaction::pointer Ledger::getSTransaction(SHAMapItem::ref item, SHAMapTreeNode::TNType type)
{
	SerializerIterator sit(item->peekSerializer());

	if (type == SHAMapTreeNode::tnTRANSACTION_NM)
		return boost::make_shared<SerializedTransaction>(boost::ref(sit));
	else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
	{
		Serializer sTxn(sit.getVL());
		SerializerIterator tSit(sTxn);
		return boost::make_shared<SerializedTransaction>(boost::ref(tSit));
	}

	return SerializedTransaction::pointer();
}

SerializedTransaction::pointer Ledger::getSMTransaction(SHAMapItem::ref item, SHAMapTreeNode::TNType type,
	TransactionMetaSet::pointer& txMeta)
{
	SerializerIterator sit(item->peekSerializer());

	if (type == SHAMapTreeNode::tnTRANSACTION_NM)
	{
		txMeta.reset();
		return boost::make_shared<SerializedTransaction>(boost::ref(sit));
	}
	else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
	{
		Serializer sTxn(sit.getVL());
		SerializerIterator tSit(sTxn);

		txMeta = boost::make_shared<TransactionMetaSet>(item->getTag(), mLedgerSeq, sit.getVL());
		return boost::make_shared<SerializedTransaction>(boost::ref(tSit));
	}

	txMeta.reset();
	return SerializedTransaction::pointer();
}

bool Ledger::getTransaction(const uint256& txID, Transaction::pointer& txn, TransactionMetaSet::pointer& meta)
{
	SHAMapTreeNode::TNType type;
	SHAMapItem::pointer item = mTransactionMap->peekItem(txID, type);
	if (!item)
		return false;

	if (type == SHAMapTreeNode::tnTRANSACTION_NM)
	{ // in tree with no metadata
		txn = theApp->getMasterTransaction().fetch(txID, false);
		meta.reset();
		if (!txn)
			txn = Transaction::sharedTransaction(item->peekData(), true);
	}
	else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
	{ // in tree with metadata
		SerializerIterator it(item->peekSerializer());
		txn = theApp->getMasterTransaction().fetch(txID, false);
		if (!txn)
			txn = Transaction::sharedTransaction(it.getVL(), true);
		else
			it.getVL(); // skip transaction
		meta = boost::make_shared<TransactionMetaSet>(txID, mLedgerSeq, it.getVL());
	}
	else
		return false;

	if (txn->getStatus() == NEW)
		txn->setStatus(mClosed ? COMMITTED : INCLUDED, mLedgerSeq);
	theApp->getMasterTransaction().canonicalize(txn, false);
	return true;
}

uint256 Ledger::getHash()
{
	if (!mValidHash)
		updateHash();
	return mHash;
}

void Ledger::saveAcceptedLedger(bool fromConsensus, LoadEvent::pointer event)
{ // can be called in a different thread
	cLog(lsTRACE) << "saveAcceptedLedger " << (fromConsensus ? "fromConsensus " : "fromAcquire ") << getLedgerSeq();
	static boost::format ledgerExists("SELECT LedgerSeq FROM Ledgers where LedgerSeq = %d;");
	static boost::format deleteLedger("DELETE FROM Ledgers WHERE LedgerSeq = %d;");
	static boost::format AcctTransExists("SELECT LedgerSeq FROM AccountTransactions WHERE TransId = '%s';");
	static boost::format transExists("SELECT Status FROM Transactions WHERE TransID = '%s';");
	static boost::format
		updateTx("UPDATE Transactions SET LedgerSeq = %d, Status = '%c', TxnMeta = %s WHERE TransID = '%s';");
	static boost::format addLedger("INSERT INTO Ledgers "
		"(LedgerHash,LedgerSeq,PrevHash,TotalCoins,ClosingTime,PrevClosingTime,CloseTimeRes,CloseFlags,"
		"AccountSetHash,TransSetHash) VALUES ('%s','%u','%s','%s','%u','%u','%d','%u','%s','%s');");

	if (!getAccountHash().isNonZero())
	{
		cLog(lsFATAL) << "AH is zero: " << getJson(0);
		assert(false);
	}

	assert (getAccountHash() == mAccountStateMap->getHash());
	assert (getTransHash() == mTransactionMap->getHash());

	{
		ScopedLock sl(theApp->getLedgerDB()->getDBLock());

		if (SQL_EXISTS(theApp->getLedgerDB()->getDB(), boost::str(ledgerExists % mLedgerSeq)))
			theApp->getLedgerDB()->getDB()->executeSQL(boost::str(deleteLedger % mLedgerSeq));

		SHAMap& txSet = *peekTransactionMap();
		Database *db = theApp->getTxnDB()->getDB();
		ScopedLock dbLock(theApp->getTxnDB()->getDBLock());
		db->executeSQL("BEGIN TRANSACTION;");
		SHAMapTreeNode::TNType type;
		for (SHAMapItem::pointer item = txSet.peekFirstItem(type); !!item;
			item = txSet.peekNextItem(item->getTag(), type))
		{
			assert(type == SHAMapTreeNode::tnTRANSACTION_MD);
			SerializerIterator sit(item->peekSerializer());
			Serializer rawTxn(sit.getVL());
			std::string escMeta(sqlEscape(sit.getVL()));

			SerializerIterator txnIt(rawTxn);
			SerializedTransaction txn(txnIt);
			assert(txn.getTransactionID() == item->getTag());

			// Make sure transaction is in AccountTransactions.
			if (!SQL_EXISTS(db, boost::str(AcctTransExists % item->getTag().GetHex())))
			{
				// Transaction not in AccountTransactions
				std::vector<RippleAddress> accts = txn.getAffectedAccounts();

				std::string sql = "INSERT INTO AccountTransactions (TransID, Account, LedgerSeq) VALUES ";
				bool first = true;
				for (std::vector<RippleAddress>::iterator it = accts.begin(), end = accts.end(); it != end; ++it)
				{
					if (!first)
						sql += ", ('";
					else
					{
						sql += "('";
						first = false;
					}
					sql += txn.getTransactionID().GetHex();
					sql += "','";
					sql += it->humanAccountID();
					sql += "',";
					sql += boost::lexical_cast<std::string>(getLedgerSeq());
					sql += ")";
				}
				sql += ";";
				Log(lsTRACE) << "ActTx: " << sql;
				db->executeSQL(sql); // may already be in there
			}

			if (SQL_EXISTS(db, boost::str(transExists %	txn.getTransactionID().GetHex())))
			{
				// In Transactions, update LedgerSeq, metadata and Status.
				db->executeSQL(boost::str(updateTx
					% getLedgerSeq()
					% TXN_SQL_VALIDATED
					% escMeta
					% txn.getTransactionID().GetHex()));
			}
			else
			{
				// Not in Transactions, insert the whole thing..
				db->executeSQL(
					txn.getMetaSQLInsertHeader() + txn.getMetaSQL(getLedgerSeq(), escMeta) + ";");
			}
		}
		db->executeSQL("COMMIT TRANSACTION;");

		theApp->getHashedObjectStore().waitWrite(); // wait until all nodes are written
		theApp->getLedgerDB()->getDB()->executeSQL(boost::str(addLedger %
			getHash().GetHex() % mLedgerSeq % mParentHash.GetHex() %
			boost::lexical_cast<std::string>(mTotCoins) % mCloseTime % mParentCloseTime %
			mCloseResolution % mCloseFlags %
			mAccountHash.GetHex() % mTransHash.GetHex()));
	}

	if (!fromConsensus)
	{
		decPendingSaves();
		return;
	}

	theApp->getLedgerMaster().setFullLedger(shared_from_this());
	event->stop();

	theApp->getOPs().pubLedger(shared_from_this());

	decPendingSaves();
}

Ledger::pointer Ledger::getSQL(const std::string& sql)
{
	uint256 ledgerHash, prevHash, accountHash, transHash;
	uint64 totCoins;
	uint32 closingTime, prevClosingTime, ledgerSeq;
	int closeResolution;
	unsigned closeFlags;
	std::string hash;

	{
		Database *db = theApp->getLedgerDB()->getDB();
		ScopedLock sl(theApp->getLedgerDB()->getDBLock());

		if (!db->executeSQL(sql) || !db->startIterRows())
		{
			cLog(lsDEBUG) << "No ledger for query: " << sql;
			return Ledger::pointer();
		}

		db->getStr("LedgerHash", hash);
		ledgerHash.SetHex(hash);
		db->getStr("PrevHash", hash);
		prevHash.SetHex(hash);
		db->getStr("AccountSetHash", hash);
		accountHash.SetHex(hash);
		db->getStr("TransSetHash", hash);
		transHash.SetHex(hash);
		totCoins = db->getBigInt("TotalCoins");
		closingTime = db->getBigInt("ClosingTime");
		prevClosingTime = db->getBigInt("PrevClosingTime");
		closeResolution = db->getBigInt("CloseTimeRes");
		closeFlags = db->getBigInt("CloseFlags");
		ledgerSeq = db->getBigInt("LedgerSeq");
		db->endIterRows();
	}

	Log(lsTRACE) << "Constructing ledger " << ledgerSeq << " from SQL";
	Ledger::pointer ret = boost::make_shared<Ledger>(prevHash, transHash, accountHash, totCoins,
		closingTime, prevClosingTime, closeFlags, closeResolution, ledgerSeq);
	if (ret->getHash() != ledgerHash)
	{
		if (sLog(lsERROR))
		{
			Log(lsERROR) << "Failed on ledger";
			Json::Value p;
			ret->addJson(p, LEDGER_JSON_FULL);
			Log(lsERROR) << p;
		}
		assert(false);
		return Ledger::pointer();
	}
	Log(lsDEBUG) << "Loaded ledger: " << ledgerHash;
	return ret;
}

Ledger::pointer Ledger::loadByIndex(uint32 ledgerIndex)
{
	std::string sql="SELECT * from Ledgers WHERE LedgerSeq='";
	sql.append(boost::lexical_cast<std::string>(ledgerIndex));
	sql.append("';");
	return getSQL(sql);
}

Ledger::pointer Ledger::loadByHash(const uint256& ledgerHash)
{
	std::string sql="SELECT * from Ledgers WHERE LedgerHash='";
	sql.append(ledgerHash.GetHex());
	sql.append("';");
	return getSQL(sql);
}

Ledger::pointer Ledger::getLastFullLedger()
{
	try
	{
		return getSQL("SELECT * from Ledgers order by LedgerSeq desc limit 1;");
	}
	catch (SHAMapMissingNode& sn)
	{
		cLog(lsWARNING) << "Database contains ledger with missing nodes: " << sn;
		return Ledger::pointer();
	}
}

void Ledger::addJson(Json::Value& ret, int options)
{
	ret["ledger"] = getJson(options);
}

Json::Value Ledger::getJson(int options)
{
	Json::Value ledger(Json::objectValue);

	boost::recursive_mutex::scoped_lock sl(mLock);
	ledger["parentHash"] = mParentHash.GetHex();

	bool full = (options & LEDGER_JSON_FULL) != 0;
	if(mClosed || full)
	{
		if (mClosed)
			ledger["closed"] = true;
		ledger["hash"] = mHash.GetHex();
		ledger["transactionHash"] = mTransHash.GetHex();
		ledger["accountHash"] = mAccountHash.GetHex();
		ledger["accepted"] = mAccepted;
		ledger["totalCoins"] = boost::lexical_cast<std::string>(mTotCoins);
		if (mCloseTime != 0)
		{
			if ((mCloseFlags & sLCF_NoConsensusTime) != 0)
				ledger["closeTimeEstimate"] = boost::posix_time::to_simple_string(ptFromSeconds(mCloseTime));
			else
			{
				ledger["closeTime"] = boost::posix_time::to_simple_string(ptFromSeconds(mCloseTime));
				ledger["closeTimeResolution"] = mCloseResolution;
			}
		}
	}
	else
		ledger["closed"] = false;
	if (mTransactionMap && (full || ((options & LEDGER_JSON_DUMP_TXRP) != 0)))
	{
		Json::Value txns(Json::arrayValue);
		SHAMapTreeNode::TNType type;
		for (SHAMapItem::pointer item = mTransactionMap->peekFirstItem(type); !!item;
				item = mTransactionMap->peekNextItem(item->getTag(), type))
		{
			if (full)
			{
				if (type == SHAMapTreeNode::tnTRANSACTION_NM)
				{
					SerializerIterator sit(item->peekSerializer());
					SerializedTransaction txn(sit);
					txns.append(txn.getJson(0));
				}
				else if (type == SHAMapTreeNode::tnTRANSACTION_MD)
				{
					SerializerIterator sit(item->peekSerializer());
					Serializer sTxn(sit.getVL());

					SerializerIterator tsit(sTxn);
					SerializedTransaction txn(tsit);

					TransactionMetaSet meta(item->getTag(), mLedgerSeq, sit.getVL());
					Json::Value txJson = txn.getJson(0);
					txJson["metaData"] = meta.getJson(0);
					txns.append(txJson);
				}
				else
				{
					Json::Value error = Json::objectValue;
					error[item->getTag().GetHex()] = type;
					txns.append(error);
				}
			}
			else txns.append(item->getTag().GetHex());
		}
		ledger["transactions"] = txns;
	}
	if (mAccountStateMap && (full || ((options & LEDGER_JSON_DUMP_STATE) != 0)))
	{
		Json::Value state(Json::arrayValue);
		for (SHAMapItem::pointer item = mAccountStateMap->peekFirstItem(); !!item;
				item = mAccountStateMap->peekNextItem(item->getTag()))
		{
			if (full)
			{
				SerializerIterator sit(item->peekSerializer());
				SerializedLedgerEntry sle(sit, item->getTag());
				state.append(sle.getJson(0));
			}
			else
				state.append(item->getTag().GetHex());
		}
		ledger["accountState"] = state;
	}
	ledger["seqNum"] = boost::lexical_cast<std::string>(mLedgerSeq);
	return ledger;
}

void Ledger::setAcquiring(void)
{
	if (!mTransactionMap || !mAccountStateMap) throw std::runtime_error("invalid map");
	mTransactionMap->setSynching();
	mAccountStateMap->setSynching();
}

bool Ledger::isAcquiring(void)
{
	return isAcquiringTx() || isAcquiringAS();
}

bool Ledger::isAcquiringTx(void)
{
	return mTransactionMap->isSynching();
}

bool Ledger::isAcquiringAS(void)
{
	return mAccountStateMap->isSynching();
}

boost::posix_time::ptime Ledger::getCloseTime() const
{
	return ptFromSeconds(mCloseTime);
}

void Ledger::setCloseTime(boost::posix_time::ptime ptm)
{
	assert(!mImmutable);
	mCloseTime = iToSeconds(ptm);
}

// XXX Use shared locks where possible?
LedgerStateParms Ledger::writeBack(LedgerStateParms parms, SLE::ref entry)
{
	ScopedLock l(mAccountStateMap->Lock());
	bool create = false;

	if (!mAccountStateMap->hasItem(entry->getIndex()))
	{
		if ((parms & lepCREATE) == 0)
		{
			Log(lsERROR) << "WriteBack non-existent node without create";
			return lepMISSING;
		}
		create = true;
	}

	SHAMapItem::pointer item = boost::make_shared<SHAMapItem>(entry->getIndex());
	entry->add(item->peekSerializer());

	if (create)
	{
		assert(!mAccountStateMap->hasItem(entry->getIndex()));
		if(!mAccountStateMap->addGiveItem(item, false, false))
		{
			assert(false);
			return lepERROR;
		}
		return lepCREATED;
	}

	if (!mAccountStateMap->updateGiveItem(item, false, false))
	{
		assert(false);
		return lepERROR;
	}
	return lepOKAY;
}

SLE::pointer Ledger::getSLE(const uint256& uHash)
{
	SHAMapItem::pointer node = mAccountStateMap->peekItem(uHash);
	if (!node)
		return SLE::pointer();
	return boost::make_shared<SLE>(node->peekSerializer(), node->getTag());
}

uint256 Ledger::getFirstLedgerIndex()
{
	SHAMapItem::pointer node = mAccountStateMap->peekFirstItem();
	return node ? node->getTag() : uint256();
}

uint256 Ledger::getLastLedgerIndex()
{
	SHAMapItem::pointer node = mAccountStateMap->peekLastItem();
	return node ? node->getTag() : uint256();
}

uint256 Ledger::getNextLedgerIndex(const uint256& uHash)
{
	SHAMapItem::pointer node = mAccountStateMap->peekNextItem(uHash);
	return node ? node->getTag() : uint256();
}

uint256 Ledger::getNextLedgerIndex(const uint256& uHash, const uint256& uEnd)
{
	SHAMapItem::pointer node = mAccountStateMap->peekNextItem(uHash);
	if ((!node) || (node->getTag() > uEnd))
		return uint256();
	return node->getTag();
}

uint256 Ledger::getPrevLedgerIndex(const uint256& uHash)
{
	SHAMapItem::pointer node = mAccountStateMap->peekPrevItem(uHash);
	return node ? node->getTag() : uint256();
}

uint256 Ledger::getPrevLedgerIndex(const uint256& uHash, const uint256& uBegin)
{
	SHAMapItem::pointer node = mAccountStateMap->peekNextItem(uHash);
	if ((!node) || (node->getTag() < uBegin))
		return uint256();
	return node->getTag();
}

SLE::pointer Ledger::getASNode(LedgerStateParms& parms, const uint256& nodeID,
	LedgerEntryType let )
{
	SHAMapItem::pointer account = mAccountStateMap->peekItem(nodeID);

	if (!account)
	{
		if ( (parms & lepCREATE) == 0 )
		{
			parms = lepMISSING;
			return SLE::pointer();
		}

		parms = parms | lepCREATED | lepOKAY;
		SLE::pointer sle=boost::make_shared<SLE>(let, nodeID);

		return sle;
	}

	SLE::pointer sle =
		boost::make_shared<SLE>(account->peekSerializer(), nodeID);

	if (sle->getType() != let)
	{ // maybe it's a currency or something
		parms = parms | lepWRONGTYPE;
		return SLE::pointer();
	}

	parms = parms | lepOKAY;

	return sle;
}

SLE::pointer Ledger::getAccountRoot(const uint160& accountID)
{
	LedgerStateParms	qry			= lepNONE;

	return getASNode(qry, getAccountRootIndex(accountID), ltACCOUNT_ROOT);
}

SLE::pointer Ledger::getAccountRoot(const RippleAddress& naAccountID)
{
	LedgerStateParms	qry			= lepNONE;

	return getASNode(qry, getAccountRootIndex(naAccountID.getAccountID()), ltACCOUNT_ROOT);
}

//
// Directory
//

SLE::pointer Ledger::getDirNode(LedgerStateParms& parms, const uint256& uNodeIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNodeIndex, ltDIR_NODE);
}

//
// Generator Map
//

SLE::pointer Ledger::getGenerator(LedgerStateParms& parms, const uint160& uGeneratorID)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, getGeneratorIndex(uGeneratorID), ltGENERATOR_MAP);
}

//
// Nickname
//

SLE::pointer Ledger::getNickname(LedgerStateParms& parms, const uint256& uNickname)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNickname, ltNICKNAME);
}

//
// Offer
//


SLE::pointer Ledger::getOffer(LedgerStateParms& parms, const uint256& uIndex)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uIndex, ltOFFER);
}

//
// Ripple State
//

SLE::pointer Ledger::getRippleState(LedgerStateParms& parms, const uint256& uNode)
{
	ScopedLock l(mAccountStateMap->Lock());

	return getASNode(parms, uNode, ltRIPPLE_STATE);
}

// For an entry put in the 64 bit index or quality.
uint256 Ledger::getQualityIndex(const uint256& uBase, const uint64 uNodeDir)
{
	// Indexes are stored in big endian format: they print as hex as stored.
	// Most significant bytes are first.  Least significant bytes represent adjacent entries.
	// We place uNodeDir in the 8 right most bytes to be adjacent.
	// Want uNodeDir in big endian format so ++ goes to the next entry for indexes.
	uint256	uNode(uBase);

	((uint64*) uNode.end())[-1]	= htobe64(uNodeDir);

	return uNode;
}

// Return the last 64 bits.
uint64 Ledger::getQuality(const uint256& uBase)
{
	return be64toh(((uint64*) uBase.end())[-1]);
}

uint256 Ledger::getQualityNext(const uint256& uBase)
{
	static	uint256	uNext("10000000000000000");

	uint256	uResult	= uBase;

	uResult += uNext;

	return uResult;
}

uint256 Ledger::getAccountRootIndex(const uint160& uAccountID)
{
	Serializer	s(22);

	s.add16(spaceAccount);	//  2
	s.add160(uAccountID);	// 20

	return s.getSHA512Half();
}

uint256 Ledger::getLedgerFeatureIndex()
{ // get the index of the node that holds the last 256 ledgers
	Serializer s(2);
	s.add16(spaceFeature);
	return s.getSHA512Half();
}

uint256 Ledger::getLedgerHashIndex()
{ // get the index of the node that holds the last 256 ledgers
	Serializer s(2);
	s.add16(spaceSkipList);
	return s.getSHA512Half();
}

uint256 Ledger::getLedgerHashIndex(uint32 desiredLedgerIndex)
{ // get the index of the node that holds the set of 256 ledgers that includes this ledger's hash
  // (or the first ledger after it if it's not a multiple of 256)
	Serializer s(6);
	s.add16(spaceSkipList);
	s.add32(desiredLedgerIndex >> 16);
	return s.getSHA512Half();
}

int Ledger::getLedgerHashOffset(uint32 ledgerIndex)
{ // get the offset for this ledger's hash (or the first one after it) in the every-256-ledger table
	return (ledgerIndex >> 8) % 256;
}

int Ledger::getLedgerHashOffset(uint32 desiredLedgerIndex, uint32 currentLedgerIndex)
{ // get the offset for this ledger's hash in the every-ledger table, -1 if not in it
	if (desiredLedgerIndex >= currentLedgerIndex)
		return -1;

	if (currentLedgerIndex < 256)
		return desiredLedgerIndex;

	if (desiredLedgerIndex < (currentLedgerIndex - 256))
		return -1;

	return currentLedgerIndex - desiredLedgerIndex - 1;
}

uint256 Ledger::getBookBase(const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuerID,
	const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuerID)
{
	bool		bInNative	= uTakerPaysCurrency.isZero();
	bool		bOutNative	= uTakerGetsCurrency.isZero();

	Serializer	s(82);

	s.add16(spaceBookDir);			//  2
	s.add160(uTakerPaysCurrency);	// 20
	s.add160(uTakerGetsCurrency);	// 20
	s.add160(uTakerPaysIssuerID);	// 20
	s.add160(uTakerGetsIssuerID);	// 20

	uint256	uBaseIndex	= getQualityIndex(s.getSHA512Half());	// Return with quality 0.

	cLog(lsDEBUG) << boost::str(boost::format("getBookBase(%s,%s,%s,%s) = %s")
		% STAmount::createHumanCurrency(uTakerPaysCurrency)
		% RippleAddress::createHumanAccountID(uTakerPaysIssuerID)
		% STAmount::createHumanCurrency(uTakerGetsCurrency)
		% RippleAddress::createHumanAccountID(uTakerGetsIssuerID)
		% uBaseIndex.ToString());

	assert(!bInNative || !bOutNative);						// XRP to XRP not allowed.
	assert(bInNative == uTakerPaysIssuerID.isZero());		// Make sure issuer is specified as needed.
	assert(bOutNative == uTakerGetsIssuerID.isZero());		// Make sure issuer is specified as needed.
	assert(uTakerPaysCurrency != uTakerGetsCurrency || uTakerPaysIssuerID != uTakerGetsIssuerID);	// Currencies or accounts must differ.

	return uBaseIndex;
}

uint256 Ledger::getDirNodeIndex(const uint256& uDirRoot, const uint64 uNodeIndex)
{
	if (uNodeIndex)
	{
		Serializer	s(42);

		s.add16(spaceDirNode);		//  2
		s.add256(uDirRoot);			// 32
		s.add64(uNodeIndex);		//  8

		return s.getSHA512Half();
	}
	else
	{
		return uDirRoot;
	}
}

uint256 Ledger::getGeneratorIndex(const uint160& uGeneratorID)
{
	Serializer	s(22);

	s.add16(spaceGenerator);	//  2
	s.add160(uGeneratorID);		// 20

	return s.getSHA512Half();
}

// What is important:
// --> uNickname: is a Sha256
// <-- SHA512/2: for consistency and speed in generating indexes.
uint256 Ledger::getNicknameIndex(const uint256& uNickname)
{
	Serializer	s(34);

	s.add16(spaceNickname);		//  2
	s.add256(uNickname);		// 32

	return s.getSHA512Half();
}

uint256 Ledger::getOfferIndex(const uint160& uAccountID, uint32 uSequence)
{
	Serializer	s(26);

	s.add16(spaceOffer);		//  2
	s.add160(uAccountID);		// 20
	s.add32(uSequence);			//  4

	return s.getSHA512Half();
}

uint256 Ledger::getOwnerDirIndex(const uint160& uAccountID)
{
	Serializer	s(22);

	s.add16(spaceOwnerDir);		//  2
	s.add160(uAccountID);		// 20

	return s.getSHA512Half();
}

uint256 Ledger::getRippleStateIndex(const RippleAddress& naA, const RippleAddress& naB, const uint160& uCurrency)
{
	uint160		uAID	= naA.getAccountID();
	uint160		uBID	= naB.getAccountID();
	bool		bAltB	= uAID < uBID;
	Serializer	s(62);

	s.add16(spaceRipple);			//  2
	s.add160(bAltB ? uAID : uBID);	// 20
	s.add160(bAltB ? uBID : uAID);  // 20
	s.add160(uCurrency);			// 20

	return s.getSHA512Half();
}

bool Ledger::walkLedger()
{
	std::vector<SHAMapMissingNode> missingNodes1, missingNodes2;
	mAccountStateMap->walkMap(missingNodes1, 32);
	if (sLog(lsINFO) && !missingNodes1.empty())
	{
		Log(lsINFO) << missingNodes1.size() << " missing account node(s)";
		Log(lsINFO) << "First: " << missingNodes1[0];
	}
	mTransactionMap->walkMap(missingNodes2, 32);
	if (sLog(lsINFO) && !missingNodes2.empty())
	{
		Log(lsINFO) << missingNodes2.size() << " missing transaction node(s)";
		Log(lsINFO) << "First: " << missingNodes2[0];
	}
	return missingNodes1.empty() && missingNodes2.empty();
}

bool Ledger::assertSane()
{
	if (mHash.isNonZero() && mAccountHash.isNonZero() && mAccountStateMap && mTransactionMap &&
			(mAccountHash == mAccountStateMap->getHash()) && (mTransHash == mTransactionMap->getHash()))
		return true;

	Log(lsFATAL) << "ledger is not sane";
	Json::Value j = getJson(0);
	j["accountTreeHash"] = mAccountHash.GetHex();
	j["transTreeHash"] = mTransHash.GetHex();

	assert(false);
	return false;
}

void Ledger::updateSkipList()
{ // update the skip list with the information from our previous ledger

	if (mLedgerSeq == 0) // genesis ledger has no previous ledger
		return;

	uint32 prevIndex = mLedgerSeq - 1;

	if ((prevIndex & 0xff) == 0)
	{ // update record of every 256th ledger
		uint256 hash = getLedgerHashIndex(prevIndex);
		SLE::pointer skipList = getSLE(hash);
		std::vector<uint256> hashes;

		if (!skipList)
		{
			skipList = boost::make_shared<SLE>(ltLEDGER_HASHES, hash);
			skipList->setFieldU32(sfFirstLedgerSequence, prevIndex);
		}
		else
			hashes = skipList->getFieldV256(sfHashes).peekValue();

		assert(hashes.size() <= 256);
		hashes.push_back(mParentHash);
		skipList->setFieldV256(sfHashes, STVector256(hashes));
		skipList->setFieldU32(sfLastLedgerSequence, prevIndex);

		if (writeBack(lepCREATE, skipList) == lepERROR)
		{
			assert(false);
		}
	}

	// update record of past 256 ledger
	uint256 hash = getLedgerHashIndex();
	SLE::pointer skipList = getSLE(hash);
	std::vector<uint256> hashes;
	if (!skipList)
	{
		skipList = boost::make_shared<SLE>(ltLEDGER_HASHES, hash);
		skipList->setFieldU32(sfFirstLedgerSequence, prevIndex);
	}
	else
		hashes = skipList->getFieldV256(sfHashes).peekValue();

	assert(hashes.size() <= 256);
	if (hashes.size() == 256)
		hashes.erase(hashes.begin());
	hashes.push_back(mParentHash);
	skipList->setFieldV256(sfHashes, STVector256(hashes));
	skipList->setFieldU32(sfLastLedgerSequence, prevIndex);

	if (writeBack(lepCREATE, skipList) == lepERROR)
	{
		assert(false);
	}
}

int Ledger::sPendingSaves = 0;
boost::recursive_mutex Ledger::sPendingSaveLock;

int Ledger::getPendingSaves()
{
	boost::recursive_mutex::scoped_lock sl(sPendingSaveLock);
	return sPendingSaves;
}

void Ledger::pendSave(bool fromConsensus)
{
	if (!fromConsensus && !theApp->isNewFlag(getHash(), SF_SAVED))
		return;

	boost::thread thread(boost::bind(&Ledger::saveAcceptedLedger, shared_from_this(),
		fromConsensus, theApp->getJobQueue().getLoadEvent(jtDISK)));
	thread.detach();

	boost::recursive_mutex::scoped_lock sl(sPendingSaveLock);
	++sPendingSaves;
}

void Ledger::decPendingSaves()
{
	boost::recursive_mutex::scoped_lock sl(sPendingSaveLock);
	--sPendingSaves;
}

void Ledger::ownerDirDescriber(SLE::ref sle, const uint160& owner)
{
	sle->setFieldAccount(sfOwner, owner);
}

void Ledger::qualityDirDescriber(SLE::ref sle,
	const uint160& uTakerPaysCurrency, const uint160& uTakerPaysIssuer,
	const uint160& uTakerGetsCurrency, const uint160& uTakerGetsIssuer,
	const uint64& uRate)
{
	sle->setFieldH160(sfTakerPaysCurrency, uTakerPaysCurrency);
	sle->setFieldH160(sfTakerPaysIssuer, uTakerPaysIssuer);
	sle->setFieldH160(sfTakerGetsCurrency, uTakerGetsCurrency);
	sle->setFieldH160(sfTakerGetsIssuer, uTakerGetsIssuer);
	sle->setFieldU64(sfExchangeRate, uRate);
}


// vim:ts=4
