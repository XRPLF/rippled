#include "UniqueNodeList.h"
#include "Application.h"
#include "Conversion.h"
#include "HttpsClient.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/mem_fn.hpp>

UniqueNodeList::UniqueNodeList() : mFetchActive(0)
{
}

void UniqueNodeList::fetchResponse(const boost::system::error_code& err, std::string strResponse)
{
	std::cerr << "Fetch complete." << std::endl;
	std::cerr << "Error: " << err.message() << std::endl;

    // std::cerr << &response << std::endl;
	// HTTP/1.1 200 OK

	{
		boost::mutex::scoped_lock sl(mFetchLock);
		mFetchActive--;
	}

	std::cerr << "Fetch active: " << mFetchActive << std::endl;
	fetchNext();
}

// Get the newcoin.txt and process it.
void UniqueNodeList::fetchProcess(std::string strDomain)
{
	std::cerr << "Fetching '" NODE_FILE_NAME "' from '" << strDomain << "'." << std::endl;

	{
		boost::mutex::scoped_lock sl(mFetchLock);
		mFetchActive++;
	}

    boost::shared_ptr<HttpsClient> client(new HttpsClient(
		theApp->getIOService(),
		strDomain,
		NODE_FILE_PATH,
		443,
		NODE_FILE_BYTES_MAX
		));

	client->httpsGet(
		boost::posix_time::seconds(NODE_FETCH_SECONDS),
		boost::bind(&UniqueNodeList::fetchResponse, this, _1, _2));
}

// Try to process the next fetch.
void UniqueNodeList::fetchNext()
{
	bool		work;
	std::string	strDomain;
	{
		boost::mutex::scoped_lock sl(mFetchLock);
		work	= mFetchActive != NODE_FETCH_JOBS && !mFetchPending.empty();
		if (work) {
			strDomain	= mFetchPending.front();
			mFetchPending.pop_front();
		}
	}

	if (!strDomain.empty())
	{
		fetchProcess(strDomain);
	}
}

// Get newcoin.txt from a domain's web server.
void UniqueNodeList::fetchNode(std::string strDomain)
{
	{
		boost::mutex::scoped_lock sl(mFetchLock);

		mFetchPending.push_back(strDomain);
	}

	fetchNext();
}

void UniqueNodeList::addNode(NewcoinAddress naNodePublic, std::string strComment)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strHanko		= naNodePublic.humanHanko();
	std::string strPublicKey	= naNodePublic.humanNodePublic();
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

void UniqueNodeList::removeNode(NewcoinAddress naHanko)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strHanko	= naHanko.humanHanko();
	std::string strTmp;

	std::string strSql		= "DELETE FROM TrustedNodes where Hanko=";
	db->escape(reinterpret_cast<const unsigned char*>(strHanko.c_str()), strHanko.size(), strTmp);
	strSql.append(strTmp);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());
}

void UniqueNodeList::reset()
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strSql		= "DELETE FROM TrustedNodes";
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
