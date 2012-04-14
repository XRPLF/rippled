#include "Application.h"
#include "Conversion.h"
#include "HttpsClient.h"
#include "ParseSection.h"
#include "Serializer.h"
#include "UniqueNodeList.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/mem_fn.hpp>
#include <boost/format.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

// Gather string constants.
#define SECTION_CURRENCIES		"currencies"
#define SECTION_DOMAIN			"domain"
#define SECTION_IPS				"ips"
#define SECTION_IPS_URL			"ips_url"
#define SECTION_PUBLIC_KEY		"validation_public_key"
#define SECTION_VALIDATORS		"validators"
#define SECTION_VALIDATORS_URL	"validators_url"

// Limit pollution of database.
#define REFERRAL_VALIDATORS_MAX	50
#define REFERRAL_IPS_MAX		50

static boost::posix_time::ptime ptEpoch()
{
	return boost::posix_time::ptime(boost::gregorian::date(2000, boost::gregorian::Jan, 1));
}

static int iToSeconds(boost::posix_time::ptime ptWhen)
{
	boost::posix_time::time_duration	td	= ptWhen - ptEpoch();

	return td.total_seconds();
}

static boost::posix_time::ptime ptFromSeconds(int iSeconds)
{
	return ptEpoch() + boost::posix_time::seconds(iSeconds);
}

UniqueNodeList::UniqueNodeList(boost::asio::io_service& io_service) :
	mFetchActive(0),
	mdtFetchTimer(io_service)
{
}

void UniqueNodeList::start()
{
	fetchNext();			// Start fetching.
}

void UniqueNodeList::fetchFinish()
{
	{
		boost::mutex::scoped_lock sl(mFetchLock);
		mFetchActive--;
	}

	fetchNext();
}

void UniqueNodeList::processIps(const std::string& strSite, section::mapped_type* pmtVecStrIps)
{
	std::cerr
		<< str(boost::format("Validator: '%s' processing %d ips.")
			% strSite % ( pmtVecStrIps ? pmtVecStrIps->size() : 0))
		<< std::endl;

	// XXX Do something with ips.
}

void UniqueNodeList::processValidators(const std::string& strSite, NewcoinAddress naNodePublic, section::mapped_type* pmtVecStrValidators)
{
	Database*	db=theApp->getWalletDB()->getDB();

	std::string strEscNodePublic	= db->escape(naNodePublic.humanNodePublic());

	std::cerr
		<< str(boost::format("Validator: '%s' processing %d validators.")
			% strSite % ( pmtVecStrValidators ? pmtVecStrValidators->size() : 0))
		<< std::endl;

	// Remove all current entries Validator in ValidatorReferrals
	// XXX INDEX BY ValidatorReferralsIndex
	std::string strSql	= str(boost::format("DELETE FROM ValidatorReferrals WHERE Validator=%s;")
		% strEscNodePublic);

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	db->executeSQL(strSql.c_str());
	// XXX Check result.

	// Add new referral entries.
	if (pmtVecStrValidators->size()) {
		std::ostringstream	ossValues;

		int	i = 0;
		BOOST_FOREACH(std::string strReferral, *pmtVecStrValidators)
		{
			if (i == REFERRAL_VALIDATORS_MAX)
				break;

			ossValues <<
				str(boost::format("%s(%s,%d,%s)")
					% ( i ? "," : "") % strEscNodePublic % i % db->escape(strReferral));
			i++;

			NewcoinAddress	naValidator;

			if (naValidator.setNodePublic(strReferral))
			{
				// A public key.
				// XXX Schedule for CAS lookup.
			}
			else
			{
				// A domain.
				nodeAddDomain(strReferral, vsReferral);
			}
		}

		std::string strSql	= str(boost::format("INSERT INTO ValidatorReferrals (Validator,Entry,Referral) VALUES %s;")
			% ossValues.str());

		ScopedLock sl(theApp->getWalletDB()->getDBLock());

		db->executeSQL(strSql.c_str());
		// XXX Check result.
	}

	// XXX Set timer to cause rebuild.
}

void UniqueNodeList::responseIps(const std::string& strSite, const boost::system::error_code& err, const std::string strIpsFile)
{
	if (!err)
	{
		section			secFile		= ParseSection(strIpsFile, true);

		processIps(strSite, sectionEntries(secFile, SECTION_IPS));
	}

	fetchFinish();
}

//
// Process [ips_url]
//
void UniqueNodeList::getIpsUrl(section secSite)
{
	std::string	strIpsUrl;
	std::string	strDomain;
	std::string	strPath;

	if (sectionSingleB(secSite, SECTION_IPS_URL, strIpsUrl)
		&& !strIpsUrl.empty()
		&& HttpsClient::httpsParseUrl(strIpsUrl, strDomain, strPath))
	{
		HttpsClient::httpsGet(
			theApp->getIOService(),
			strDomain,
			443,
			strPath,
			NODE_FILE_BYTES_MAX,
			boost::posix_time::seconds(NODE_FETCH_SECONDS),
			boost::bind(&UniqueNodeList::responseIps, this, strDomain, _1, _2));
	}
	else
	{
		fetchFinish();
	}
}

void UniqueNodeList::responseValidators(NewcoinAddress naNodePublic, section secSite, const std::string& strSite, const boost::system::error_code& err, const std::string strValidatorsFile)
{
	if (!err)
	{
		section		secFile		= ParseSection(strValidatorsFile, true);

		processValidators(strSite, naNodePublic, sectionEntries(secFile, SECTION_VALIDATORS));
	}

	getIpsUrl(secSite);
}

//
// Process [validators_url]
//
void UniqueNodeList::getValidatorsUrl(NewcoinAddress naNodePublic, section secSite)
{
	std::string strValidatorsUrl;
	std::string	strDomain;
	std::string	strPath;

	if (sectionSingleB(secSite, SECTION_VALIDATORS_URL, strValidatorsUrl)
		&& !strValidatorsUrl.empty()
		&& HttpsClient::httpsParseUrl(strValidatorsUrl, strDomain, strPath))
	{
		HttpsClient::httpsGet(
			theApp->getIOService(),
			strDomain,
			443,
			strPath,
			NODE_FILE_BYTES_MAX,
			boost::posix_time::seconds(NODE_FETCH_SECONDS),
			boost::bind(&UniqueNodeList::responseValidators, this, naNodePublic, secSite, strDomain, _1, _2));
	}
	else
	{
		getIpsUrl(secSite);
	}
}

// Process a newcoin.txt.
void UniqueNodeList::processFile(const std::string strDomain, NewcoinAddress naNodePublic, section secSite)
{
	//
	// Process Validators
	//
	processValidators(strDomain, naNodePublic, sectionEntries(secSite, SECTION_VALIDATORS));

	//
	// Process ips
	//
	processIps(strDomain, sectionEntries(secSite, SECTION_IPS));

	//
	// Process currencies
	//
	section::mapped_type*	pvCurrencies;

	if ((pvCurrencies = sectionEntries(secSite, SECTION_CURRENCIES)) && pvCurrencies->size())
	{
		// XXX Process currencies.
		std::cerr << "Ignoring currencies: not implemented." << std::endl;
	}

	getValidatorsUrl(naNodePublic, secSite);
}

void UniqueNodeList::responseFetch(const std::string strDomain, const boost::system::error_code& err, const std::string strSiteFile)
{
	section				secSite	= ParseSection(strSiteFile, true);
	bool				bGood	= !err;

	if (bGood)
	{
		std::cerr << boost::format("Validator: '%s' received " NODE_FILE_NAME ".") % strDomain << std::endl;
	}
	else
	{
		std::cerr
			<< boost::format("Validator: '%s' unabile to retrieve " NODE_FILE_NAME ": %s")
				% strDomain
				% err.message()
			<< std::endl;
	}

	//
	// Verify file domain
	//
	std::string	strSite;

	if (bGood && !sectionSingleB(secSite, SECTION_DOMAIN, strSite))
	{
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " missing single entry for " SECTION_DOMAIN ".")
				% strDomain
			<< std::endl;
	}

	if (bGood && strSite != strDomain)
	{
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_DOMAIN " does not match: %s")
				% strDomain
				% strSite
			<< std::endl;
	}

	//
	// Process public key
	//
	std::string		strNodePublicKey;

	if (bGood && !sectionSingleB(secSite, SECTION_PUBLIC_KEY, strNodePublicKey))
	{
		// Bad [validation_public_key] section.
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_PUBLIC_KEY " does not have single entry.")
				% strDomain
			<< std::endl;
	}

	NewcoinAddress	naNodePublic;

	if (bGood && !naNodePublic.setNodePublic(strNodePublicKey))
	{
		// Bad public key.
		bGood	= false;

		std::cerr
			<< boost::format("Validator: '%s' bad " NODE_FILE_NAME " " SECTION_PUBLIC_KEY " is bad: ")
				% strDomain
				% strNodePublicKey
			<< std::endl;
	}

	if (bGood)
	{
// std::cerr << boost::format("naNodePublic: '%s'") % naNodePublic.humanNodePublic() << std::endl;

		seedDomain	sdCurrent;

		bool		bFound	= getSeedDomans(strDomain, sdCurrent);

		assert(bFound);

		uint256		iSha256		= Serializer::getSHA512Half(strSiteFile);
		bool		bChangedB	= sdCurrent.iSha256	!= iSha256;

		sdCurrent.strDomain		= strDomain;
		// XXX If the node public key is changing, delete old public key information?
		// XXX Only if no other refs to keep it arround, other wise we have an attack vector.
		sdCurrent.naPublicKey	= naNodePublic;

// std::cerr << boost::format("sdCurrent.naPublicKey: '%s'") % sdCurrent.naPublicKey.humanNodePublic() << std::endl;

		sdCurrent.tpFetch		= boost::posix_time::second_clock::universal_time();
		sdCurrent.iSha256		= iSha256;

		setSeedDomans(sdCurrent);

		if (bChangedB)
		{
			std::cerr << boost::format("Validator: '%s' processing new " NODE_FILE_NAME ".") % strDomain << std::endl;
			processFile(strDomain, naNodePublic, secSite);
		}
		else
		{
			std::cerr << boost::format("Validator: '%s' no change for " NODE_FILE_NAME ".") % strDomain << std::endl;
			fetchFinish();
		}
	}
	else
	{
		// Failed: Update

		// XXX If we have public key, perhaps try look up in CAS?
		fetchFinish();
	}
}

// Get the newcoin.txt and process it.
void UniqueNodeList::fetchProcess(std::string strDomain)
{
	std::cerr << "Fetching '" NODE_FILE_NAME "' from '" << strDomain << "'." << std::endl;

	std::deque<std::string>	deqSites;
	std::ostringstream		ossNewcoin;
	std::ostringstream		ossWeb;

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

void UniqueNodeList::fetchTimerHandler(const boost::system::error_code& err)
{
	if (!err)
	{
		// Time to check for another fetch.
		std::cerr << "fetchTimerHandler" << std::endl;
		fetchNext();
	}
}

// Try to process the next fetch.
void UniqueNodeList::fetchNext()
{
	bool	bFull;

	{
		boost::mutex::scoped_lock sl(mFetchLock);

		bFull	= mFetchActive == NODE_FETCH_JOBS;
	}

	if (!bFull)
	{
		// Determine next scan.
		std::string strSql("SELECT Domain,Next FROM SeedDomains ORDER BY Next ASC LIMIT 1;");
		std::string	strDomain;
		boost::posix_time::ptime	tpNext;
		boost::posix_time::ptime	tpNow;

		ScopedLock sl(theApp->getWalletDB()->getDBLock());
		Database *db=theApp->getWalletDB()->getDB();

		if (db->executeSQL(strSql.c_str()) && db->startIterRows())
		{
			int			iNext	= db->getInt("Next");

			tpNext	= ptFromSeconds(iNext);
			tpNow	= boost::posix_time::second_clock::universal_time();

			db->getStr("Domain", strDomain);

			db->endIterRows();
		}

		if (!strDomain.empty())
		{
			boost::mutex::scoped_lock sl(mFetchLock);

			bFull	= mFetchActive == NODE_FETCH_JOBS;

			if (!bFull && tpNext <= tpNow) {
				mFetchActive++;
			}
		}

		if (strDomain.empty() || bFull)
		{
			// nothing();
		}
		else if (tpNext > tpNow)
		{
			// Fetch needs to happen in the future.  Set a timer to wake us.
			mtpFetchNext	= tpNext;

			mdtFetchTimer.expires_at(mtpFetchNext);
			mdtFetchTimer.async_wait(boost::bind(&UniqueNodeList::fetchTimerHandler, this, _1));
		}
		else
		{
			// Fetch needs to happen now.
			mtpFetchNext	= boost::posix_time::ptime(boost::posix_time::not_a_date_time);

			seedDomain	sdCurrent;
			bool		bFound	= getSeedDomans(strDomain, sdCurrent);

			assert(bFound);

			// Update time of next fetch and this scan attempt.
			sdCurrent.tpScan		= tpNow;

			// XXX Use a longer duration if we have lots of validators.
			sdCurrent.tpNext		= sdCurrent.tpScan+boost::posix_time::hours(7*24);

			setSeedDomans(sdCurrent);

			std::cerr << "Validator: '" << strDomain << "' fetching " NODE_FILE_NAME "." << std::endl;

			fetchProcess(strDomain);	// Go get it.

			fetchNext();				// Look for more.
		}
	}
}

int UniqueNodeList::iSourceScore(validatorSource vsWhy)
{
	int		iScore	= 0;

	switch (vsWhy) {
	case vsManual:		iScore	= 1500; break;
	case vsValidator:	iScore	= 1000; break;
	case vsWeb:			iScore	=  200; break;
	case vsReferral:	iScore	=    0; break;
	default:
		throw std::runtime_error("Internal error: bad validatorSource.");
	}

	return iScore;
}

// Queue a domain for a single attempt fetch a newcoin.txt.
// --> strComment: only used on vsManual
// YYY As a lot of these may happen at once, would be nice to wrap multiple calls in a transaction.
void UniqueNodeList::nodeAddDomain(std::string strDomain, validatorSource vsWhy, std::string strComment)
{
	// YYY Would be best to verify strDomain is a valid domain.
	seedDomain	sdCurrent;

	bool		bFound		= getSeedDomans(strDomain, sdCurrent);
	bool		bChanged	= false;

	if (!bFound)
	{
		sdCurrent.strDomain	= strDomain;
		sdCurrent.tpNext	= boost::posix_time::second_clock::universal_time();
	}

	// Promote source, if needed.
	if (!bFound || iSourceScore(vsWhy) >= iSourceScore(sdCurrent.vsSource))
	{
		sdCurrent.vsSource	= vsWhy;
		bChanged			= true;
	}

	if (vsManual == vsWhy)
	{
		// A manual add forces immediate scan.
		sdCurrent.tpNext		= boost::posix_time::second_clock::universal_time();
		sdCurrent.strComment	= strComment;
		bChanged				= true;
	}

	if (bChanged)
		setSeedDomans(sdCurrent);
}

bool UniqueNodeList::getSeedDomans(const std::string& strDomain, seedDomain& dstSeedDomain)
{
	bool		bResult;
	Database*	db=theApp->getWalletDB()->getDB();

	std::string strSql	= str(boost::format("SELECT * FROM SeedDomains WHERE Domain=%s;")
		% db->escape(strDomain));

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	bResult	= db->executeSQL(strSql.c_str()) && db->startIterRows();
	if (bResult)
	{
		std::string		strPublicKey;
		std::string		strSource;
		int				iNext;
		int				iScan;
		int				iFetch;
		std::string		strSha256;

		db->getStr("Domain", dstSeedDomain.strDomain);

		if (!db->getNull("PublicKey") && db->getStr("PublicKey", strPublicKey))
		{
			dstSeedDomain.naPublicKey.setNodePublic(strPublicKey);
		}
		else
		{
			dstSeedDomain.naPublicKey.clear();
		}

		db->getStr("Source", strSource);
			dstSeedDomain.vsSource	= static_cast<validatorSource>(strSource[0]);
		iNext	= db->getInt("Next");
			dstSeedDomain.tpNext	= ptFromSeconds(iNext);
		iScan	= db->getInt("Scan");
			dstSeedDomain.tpScan	= ptFromSeconds(iScan);
		iFetch	= db->getInt("Fetch");
			dstSeedDomain.tpFetch	= ptFromSeconds(iFetch);
		db->getStr("Sha256", strSha256);
			dstSeedDomain.iSha256.SetHex(strSha256.c_str());
		db->getStr("Comment", dstSeedDomain.strComment);

		db->endIterRows();
	}

	return bResult;
}

void UniqueNodeList::setSeedDomans(const seedDomain& sdSource)
{
	Database*	db=theApp->getWalletDB()->getDB();

	int		iNext	= iToSeconds(sdSource.tpNext);
	int		iScan	= iToSeconds(sdSource.tpScan);
	int		iFetch	= iToSeconds(sdSource.tpFetch);

	std::string strSql	= str(boost::format("REPLACE INTO SeedDomains (Domain,PublicKey,Source,Next,Scan,Fetch,Sha256,Comment) VALUES (%s, %s, %s, %d, %d, %d, '%s', %s);")
		% db->escape(sdSource.strDomain)
		% (sdSource.naPublicKey.IsValid() ? db->escape(sdSource.naPublicKey.humanNodePublic()) : "NULL")
		% db->escape(std::string(1, static_cast<char>(sdSource.vsSource)))
		% iNext
		% iScan
		% iFetch
		% sdSource.iSha256.GetHex()
		% db->escape(sdSource.strComment)
		);

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	db->executeSQL(strSql.c_str());
	// XXX Check result.

	if (mtpFetchNext.is_not_a_date_time() || mtpFetchNext > sdSource.tpNext)
	{
		// Schedule earlier wake up.
		fetchNext();
	}
}

// XXX allow update of comment.
void UniqueNodeList::nodeAddPublic(NewcoinAddress naNodePublic, std::string strComment)
{
	Database* db=theApp->getWalletDB()->getDB();

	std::string strPublicKey	= naNodePublic.humanNodePublic();
	std::string strTmp;

	std::string strSql="INSERT INTO TrustedNodes (PublicKey,Comment) values (";
	strSql.append(db->escape(strPublicKey));
	strSql.append(",");
	strSql.append(db->escape(strComment));
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
			nodeAddDomain(strValidator, vsValidator);
		}
	}
}

// vim:ts=4
