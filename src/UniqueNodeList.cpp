#include "Application.h"
#include "Conversion.h"
#include "HttpsClient.h"
#include "ParseSection.h"
#include "UniqueNodeList.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/mem_fn.hpp>

// Gather string constants.
#define SECTION_CURRENCIES		"currencies"
#define SECTION_DOMAIN			"domain"
#define SECTION_IPS				"ips"
#define SECTION_IPS_URL			"ips_url"
#define SECTION_PUBLIC_KEY		"node_public_key"
#define SECTION_VALIDATORS		"validators"
#define SECTION_VALIDATORS_URL	"validators_url"

UniqueNodeList::UniqueNodeList() : mFetchActive(0)
{
}

void UniqueNodeList::processIps(section::mapped_type& vecStrIps) {
	std::cerr << "Processing ips." << std::endl;

	// XXX Do something with ips.
}

void UniqueNodeList::processValidators(section::mapped_type& vecStrValidators)
{
	std::cerr << "Processing validators." << std::endl;

	// XXX Do something with validators.
}

void UniqueNodeList::responseIps(const boost::system::error_code& err, const std::string strIpsFile)
{
	section					secFile		= ParseSection(strIpsFile, true);
	section::mapped_type*	pmtEntries	= sectionEntries(secFile, SECTION_IPS);

	if (pmtEntries)
		processIps(*pmtEntries);

	getValidatorsUrl();
}

void UniqueNodeList::responseValidators(const boost::system::error_code& err, const std::string strValidatorsFile)
{
	section					secFile		= ParseSection(strValidatorsFile, true);
	section::mapped_type*	pmtEntries	= sectionEntries(secFile, SECTION_VALIDATORS);

	if (pmtEntries)
		processValidators(*pmtEntries);

	getFinish();
}

void UniqueNodeList::getIpsUrl()
{
	std::string	strDomain;
	std::string	strPath;

	if (!mStrIpsUrl.empty() && HttpsClient::httpsParseUrl(mStrIpsUrl, strDomain, strPath))
	{
		HttpsClient::httpsGet(
			theApp->getIOService(),
			strDomain,
			443,
			strPath,
			NODE_FILE_BYTES_MAX,
			boost::posix_time::seconds(NODE_FETCH_SECONDS),
			boost::bind(&UniqueNodeList::responseIps, this, _1, _2));
	}
	else
	{
		getValidatorsUrl();
	}
}

void UniqueNodeList::getValidatorsUrl()
{
	std::string	strDomain;
	std::string	strPath;

	if (!mStrValidatorsUrl.empty() && HttpsClient::httpsParseUrl(mStrValidatorsUrl, strDomain, strPath))
	{
		HttpsClient::httpsGet(
			theApp->getIOService(),
			strDomain,
			443,
			strPath,
			NODE_FILE_BYTES_MAX,
			boost::posix_time::seconds(NODE_FETCH_SECONDS),
			boost::bind(&UniqueNodeList::responseValidators, this, _1, _2));
	}
	else
	{
		getFinish();
	}
}

void UniqueNodeList::getFinish()
{
	{
		boost::mutex::scoped_lock sl(mFetchLock);
		mFetchActive--;
	}

	std::cerr << "Fetch active: " << mFetchActive << std::endl;
	fetchNext();
}

void UniqueNodeList::responseFetch(const std::string strDomain, const boost::system::error_code& err, const std::string strSiteFile)
{
	std::cerr << "Fetch complete." << std::endl;
	std::cerr << "Error: " << err.message() << std::endl;

	section				secSite	= ParseSection(strSiteFile, true);
	bool				bGood	= !err;

	//
	// Verify file domain
	//
	std::string	strSite;

	if (bGood)
	{
		bGood	= sectionSingleB(secSite, SECTION_DOMAIN, strSite);
		if (strSite != strDomain)
		{
			bGood	= false;

			std::cerr << "Warning: Site '" << strDomain << "' provides '" NODE_FILE_NAME "' for '" << strSite << "', ignoring." << std::endl;
		}
		else
		{
			std::cerr << "Processing: Site '" << strDomain << "' '" NODE_FILE_NAME "'" << std::endl;
		}
	}

	//
	// Process public key
	//
	std::string				strNodePublicKey;

	if (bGood && (bGood = sectionSingleB(secSite, SECTION_PUBLIC_KEY, strNodePublicKey)))
	{
		NewcoinAddress	naNodePublic;

		if (naNodePublic.setNodePublic(strNodePublicKey))
		{
			std::cerr << strDomain << ": " << strNodePublicKey << std::endl;

			nodeAdd(naNodePublic, strDomain);
		}
	}

	//
	// Process ips
	//
	section::mapped_type*	pvIps;

	if (bGood && (pvIps = sectionEntries(secSite, SECTION_IPS)) && pvIps->size())
	{
		std::cerr << "Ips: " << pvIps->size() << std::endl;

		processIps(*pvIps);
	}

	//
	// Process ips_url
	//
	if (bGood)
		(void) sectionSingleB(secSite, SECTION_IPS_URL, mStrIpsUrl);

	//
	// Process Validators
	//
	section::mapped_type*	pvValidators;

	if (bGood && (pvValidators = sectionEntries(secSite, SECTION_VALIDATORS)) && pvValidators->size())
	{
		processValidators(*pvValidators);
	}

	//
	// Process validators_url
	//

	if (bGood)
		(void) sectionSingleB(secSite, SECTION_VALIDATORS_URL, mStrValidatorsUrl);

	//
	// Process currencies
	//
	section::mapped_type*	pvCurrencies;

	if (bGood && (pvCurrencies = sectionEntries(secSite, SECTION_CURRENCIES)) && pvCurrencies->size())
	{
		// XXX Process currencies.
		std::cerr << "Ignoring currencies: not implemented." << std::endl;
	}

	getIpsUrl();
}

// Get the newcoin.txt and process it.
void UniqueNodeList::fetchProcess(std::string strDomain)
{
	std::cerr << "Fetching '" NODE_FILE_NAME "' from '" << strDomain << "'." << std::endl;

	{
		boost::mutex::scoped_lock sl(mFetchLock);
		mFetchActive++;
	}

	std::deque<std::string>	deqSites;

	std::ostringstream	ossNewcoin;
	std::ostringstream	ossWeb;

	ossNewcoin << SYSTEM_NAME "." << strDomain;
	ossWeb << "www." << strDomain;

	deqSites.push_back(ossNewcoin.str());
	deqSites.push_back(ossWeb.str());
	deqSites.push_back(strDomain);

	HttpsClient::httpsGet(
		theApp->getIOService(),
		deqSites,
		443,
		NODE_FILE_PATH,
		NODE_FILE_BYTES_MAX,
		boost::posix_time::seconds(NODE_FETCH_SECONDS),
		boost::bind(&UniqueNodeList::responseFetch, this, strDomain, _1, _2));
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
void UniqueNodeList::nodeFetch(std::string strDomain)
{
	{
		boost::mutex::scoped_lock sl(mFetchLock);

		mFetchPending.push_back(strDomain);
	}

	fetchNext();
}

// XXX allow update of comment.
void UniqueNodeList::nodeAdd(NewcoinAddress naNodePublic, std::string strComment)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strPublicKey	= naNodePublic.humanNodePublic();
	std::string strTmp;

	std::string strSql="INSERT INTO TrustedNodes (PublicKey,Comment) values (";
	db->escape(reinterpret_cast<const unsigned char*>(strPublicKey.c_str()), strPublicKey.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(",");
	db->escape(reinterpret_cast<const unsigned char*>(strComment.c_str()), strComment.size(), strTmp);
	strSql.append(strTmp);
	strSql.append(")");

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());
}

void UniqueNodeList::nodeRemove(NewcoinAddress naNodePublic)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strPublic	= naNodePublic.humanNodePublic();
	std::string strTmp;

	std::string strSql		= "DELETE FROM TrustedNodes where PublicKey=";
	db->escape(reinterpret_cast<const unsigned char*>(strPublic.c_str()), strPublic.size(), strTmp);
	strSql.append(strTmp);

	ScopedLock sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());
}

void UniqueNodeList::nodeReset()
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
			std::string	strPublicKey;
			std::string	strComment;

			db->getStr("PublicKey", strPublicKey);
			db->getStr("Comment", strComment);

			Json::Value node(Json::objectValue);

			node["PublicKey"]	= strPublicKey;
			node["Comment"]		= strComment;

			ret.append(node);

			more	= db->getNextRow();
		}

		db->endIterRows();
	}

	return ret;
}

void UniqueNodeList::nodeDefault(std::string strValidators) {
	section secValidators	= ParseSection(strValidators, true);

	section::mapped_type*	pmtEntries	= sectionEntries(secValidators, SECTION_VALIDATORS);
	if (pmtEntries)
	{
		BOOST_FOREACH(std::string strValidator, *pmtEntries)
		{
			nodeFetch(strValidator);
		}
	}
}

// vim:ts=4
