
#include "Application.h"
#include "Log.h"

static std::vector<std::string> getSchema(DatabaseCon* dbc, const std::string& dbName)
{
	std::vector<std::string> schema;

	std::string sql = "SELECT sql FROM sqlite_master WHERE tbl_name='";
	sql += dbName;
	sql += "';";

	SQL_FOREACH(dbc->getDB(), sql)
	{
			dbc->getDB()->getStr("sql", sql);
			schema.push_back(sql);
	}

	return schema;
}

static bool schemaHas(DatabaseCon* dbc, const std::string& dbName, int line, const std::string& content)
{
	std::vector<std::string> schema = getSchema(dbc, dbName);
	if (schema.size() <= line)
	{
		Log(lsFATAL) << "Schema for " << dbName << " has too few lines";
		throw std::runtime_error("bad schema");
	}
	return schema[line].find(content) != std::string::npos;
}

static void addTxnSeqField()
{
	if (schemaHas(theApp->getTxnDB(), "AccountTransactions", 0, "TxnSeq"))
		return;
	Log(lsWARNING) << "Transaction sequence field is missing";

	Database* db = theApp->getTxnDB()->getDB();

	std::vector< std::pair<uint256, int> > txIDs;
	txIDs.reserve(300000);

	Log(lsINFO) << "Parsing transactions";
	int i = 0;
	uint256 transID;
	SQL_FOREACH(db, "SELECT TransID,TxnMeta FROM Transactions;")
	{
		std::vector<unsigned char> rawMeta;
		int metaSize = 2048;
		rawMeta.resize(metaSize);
		metaSize = db->getBinary("TxnMeta", &*rawMeta.begin(), rawMeta.size());
		if (metaSize > rawMeta.size())
		{
			rawMeta.resize(metaSize);
			db->getBinary("TxnMeta", &*rawMeta.begin(), rawMeta.size());
		}
		else rawMeta.resize(metaSize);

		std::string tid;
		db->getStr("TransID", tid);
		transID.SetHex(tid, true);

		if (rawMeta.size() == 0)
		{
			txIDs.push_back(std::make_pair(transID, -1));
			Log(lsINFO) << "No metadata for " << transID;
		}
		else
		{
			TransactionMetaSet m(transID, 0, rawMeta);
			txIDs.push_back(std::make_pair(transID, m.getIndex()));
		}

		if ((++i % 1000) == 0)
			Log(lsINFO) << i << " transactions read";
	}

	Log(lsINFO) << "All " << i << " transactions read";

	db->executeSQL("BEGIN TRANSACTION;");

	Log(lsINFO) << "Dropping old index";
	db->executeSQL("DROP INDEX AcctTxIndex;");

	Log(lsINFO) << "Altering table";
	db->executeSQL("ALTER TABLE AccountTransactions ADD COLUMN TxnSeq INTEGER;");

	typedef std::pair<uint256, int> u256_int_pair_t;
	boost::format fmt("UPDATE AccountTransactions SET TxnSeq = %d WHERE TransID = '%s';");
	i = 0;
	BOOST_FOREACH(u256_int_pair_t& t, txIDs)
	{
		db->executeSQL(boost::str(fmt % t.second % t.first.GetHex()));
		if ((++i % 1000) == 0)
			Log(lsINFO) << i << " transactions updated";
	}

	db->executeSQL("CREATE INDEX AcctTxIndex ON AccountTransactions(Account, LedgerSeq, TxnSeq, TransID);");
	db->executeSQL("END TRANSACTION;");
}

void Application::updateTables()
{ // perform any needed table updates
	assert(schemaHas(theApp->getTxnDB(), "AccountTransactions", 0, "TransID"));
	assert(!schemaHas(theApp->getTxnDB(), "AccountTransactions", 0, "foobar"));
	addTxnSeqField();
}
