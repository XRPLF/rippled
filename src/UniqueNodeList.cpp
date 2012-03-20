#include "UniqueNodeList.h"
#include "Application.h"
#include "Conversion.h"

void UniqueNodeList::addNode(NewcoinAddress nodePublic, std::string strComment)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strHanko	    = nodePublic.humanHanko();
	std::string strPublicKey   = nodePublic.humanNodePublic();
	std::string strTmp;

	std::string strSql="INSERT INTO TrustedNodes (Hanko,PublicKey,Comment) values (";
	db->escape(reinterpret_cast<const unsigned char*>(strHanko.c_str()), strHanko.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(",");
	db->escape(reinterpret_cast<const unsigned char*>(strPublicKey.c_str()), strPublicKey.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(",");
	db->escape(reinterpret_cast<const unsigned char*>(strComment.c_str()), strComment.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(")");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());
}

void UniqueNodeList::removeNode(NewcoinAddress hanko)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strHanko	= hanko.humanHanko();
	std::string strTmp;

	std::string strSql		= "DELETE FROM TrustedNodes where Hanko=";
	db->escape(reinterpret_cast<const unsigned char*>(strHanko.c_str()), strHanko.size(), strTmp);
	strSql.append(strTmp);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());
}

// 0- we don't care, 1- we care and is valid, 2-invalid signature
#if 0
int UniqueNodeList::checkValid(newcoin::Validation& valid)
{
	Database* db=theApp->getWalletDB()->getDB();
	std::string strSql="SELECT pubkey from TrustedNodes where hanko=";
	std::string hashStr;
	db->escape((unsigned char*) &(valid.hanko()[0]),valid.hanko().size(),hashStr);
	strSql.append(hashStr);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	if( db->executeSQL(strSql.c_str()) )
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

Json::Value UniqueNodeList::getUnlJson()
{
	Database* db=theApp->getWalletDB()->getDB();
	std::string strSql="SELECT * FROM TrustedNodes;";

    Json::Value ret(Json::arrayValue);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	if( db->executeSQL(strSql.c_str()) )
	{
		bool	more	= db->startIterRows();
		while (more)
		{
			std::string	strHanko;
			std::string	strPublicKey;
			std::string	strComment;

			db->getStr("Hanko", strHanko);
			db->getStr("PublicKey", strPublicKey);
			db->getStr("Comment", strComment);

			Json::Value node(Json::objectValue);

			node["Hanko"]		= strHanko;
			node["PublicKey"]	= strPublicKey;
			node["Comment"]		= strComment;

			ret.append(node);

			more	= db->getNextRow();
		}

		db->endIterRows();
	}

	return ret;
}
// vim:ts=4
