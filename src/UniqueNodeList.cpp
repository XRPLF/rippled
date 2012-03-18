#include "UniqueNodeList.h"
#include "Application.h"
#include "Conversion.h"

void UniqueNodeList::addNode(NewcoinAddress address,std::string comment)
{
	Database* db=theApp->getWalletDB()->getDB();

	// void UniqueNodeList::addNode(uint160& hanko, std::vector<unsigned char>& publicKey,std::string comment)

	std::string hanko	= address.humanHanko();
	std::string publicKey	= address.humanNodePublic();

	std::string sql="INSERT INTO TrustedNodes (Hanko,PubKey,Comment) values (";
	std::string tmpStr;
	db->escape(reinterpret_cast<const unsigned char*>(hanko.c_str()), hanko.size(), tmpStr);
	sql.append(tmpStr);
	sql.append(",");
	db->escape(reinterpret_cast<const unsigned char*>(publicKey.c_str()), publicKey.size(), tmpStr);
	sql.append(tmpStr);
	sql.append(",");
	db->escape(reinterpret_cast<const unsigned char*>(comment.c_str()), comment.size(), tmpStr);
	sql.append(tmpStr);
	sql.append(")");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(sql.c_str());
}

void UniqueNodeList::removeNode(uint160& hanko)
{
	Database* db=theApp->getWalletDB()->getDB();
	std::string sql="DELETE FROM TrustedNodes where hanko=";
	std::string hashStr;
	db->escape(hanko.begin(), hanko.GetSerializeSize(), hashStr);
	sql.append(hashStr);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(sql.c_str());
}

// 0- we don't care, 1- we care and is valid, 2-invalid signature
#if 0
int UniqueNodeList::checkValid(newcoin::Validation& valid)
{
	Database* db=theApp->getWalletDB()->getDB();
	std::string sql="SELECT pubkey from TrustedNodes where hanko=";
	std::string hashStr;
	db->escape((unsigned char*) &(valid.hanko()[0]),valid.hanko().size(),hashStr);
	sql.append(hashStr);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	if( db->executeSQL(sql.c_str()) )
	{
		if(db->startIterRows() && db->getNextRow())
		{
			//theApp->getDB()->getBytes();

			// TODO: check that the public key makes the correct signature of the validation
			db->endIterRows();
			return(1);
		}
		else  db->endIterRows();
	}
	return(0); // not on our list
}
#endif

void UniqueNodeList::dumpUNL(std::string& retStr)
{
	Database* db=theApp->getWalletDB()->getDB();
	std::string sql="SELECT * FROM TrustedNodes;";

	retStr.append("hello\n");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	if( db->executeSQL(sql.c_str()) )
	{
		db->startIterRows();
		while(db->getNextRow())
		{
			uint160 hanko;
			db->getBinary("Hanko", hanko.begin(), hanko.GetSerializeSize());
			std::string tstr;
			u160ToHuman(hanko, tstr);

			retStr.append(tstr);
			retStr.append("\n");
		}
		db->endIterRows();
	}
}

