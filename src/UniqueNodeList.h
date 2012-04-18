#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__

#include <deque>

#include "../json/value.h"

#include "NewcoinAddress.h"

#include "HttpsClient.h"
#include "ParseSection.h"

#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>

#define SYSTEM_NAME	"newcoin"

// Guarantees minimum thoughput of 1 node per second.
#define NODE_FETCH_JOBS			10
#define NODE_FETCH_SECONDS		10
#define NODE_FILE_BYTES_MAX		(50<<10)	// 50k
#define NODE_FILE_NAME			SYSTEM_NAME ".txt"
#define NODE_FILE_PATH			"/" NODE_FILE_NAME

// Wait for validation information to be stable before scoring.
// #define SCORE_DELAY_SECONDS		20
#define SCORE_DELAY_SECONDS		5

// Don't bother propagating past this number of rounds.
#define SCORE_ROUNDS			10

class UniqueNodeList
{
public:
	typedef enum {
		vsManual	= 'M',
		vsValidator	= 'V',	// validators.txt
		vsWeb		= 'W',
		vsReferral	= 'R',
	} validatorSource;

private:
	// Misc persistent information
	boost::posix_time::ptime		mtpScoreUpdated;
	boost::posix_time::ptime		mtpFetchUpdated;

	bool	miscLoad();
	bool	miscSave();

	typedef struct {
		std::string					strDomain;
		NewcoinAddress				naPublicKey;
		validatorSource				vsSource;
		boost::posix_time::ptime	tpNext;
		boost::posix_time::ptime	tpScan;
		boost::posix_time::ptime	tpFetch;
		uint256						iSha256;
		std::string					strComment;
	} seedDomain;

	typedef struct {
		int					iScore;
		int					iRoundScore;
		int					iRoundSeed;
		int					iSeen;
		std::string			strValidator;	// The public key.
		std::vector<int>	viReferrals;
	} scoreNode;

	typedef boost::unordered_map<std::string,int> strIndex;

	bool scoreRound(std::vector<scoreNode>& vsnNodes);
	int iSourceScore(validatorSource vsWhy);

	void responseFetch(const std::string strDomain, const boost::system::error_code& err, const std::string strSiteFile);

	boost::posix_time::ptime		mtpScoreNext;		// When to start scoring.
	boost::posix_time::ptime		mtpScoreStart;		// Time currently started scoring.
	boost::asio::deadline_timer		mdtScoreTimer;		// Timer to start scoring.

	void scoreNext();									// Update scoring timer.
	void scoreCompute();
	void scoreTimerHandler(const boost::system::error_code& err);

	boost::mutex					mFetchLock;
	int								mFetchActive;		// Count of active fetches.

	boost::posix_time::ptime		mtpFetchNext;		// Time of to start next fetch.
	boost::asio::deadline_timer		mdtFetchTimer;		// Timer to start fetching.

	void fetchNext();
	void fetchFinish();
	void fetchProcess(std::string strDomain);
	void fetchTimerHandler(const boost::system::error_code& err);

	void getValidatorsUrl(NewcoinAddress naNodePublic, section secSite);
	void getIpsUrl(section secSite);
	void responseIps(const std::string& strSite, const boost::system::error_code& err, const std::string strIpsFile);
	void responseValidators(const std::string& strValidatorsUrl, NewcoinAddress naNodePublic, section secSite, const std::string& strSite, const boost::system::error_code& err, const std::string strValidatorsFile);

	void processIps(const std::string& strSite, section::mapped_type* pmtVecStrIps);
	void processValidators(const std::string& strSite, const std::string& strValidatorsSrc, NewcoinAddress naNodePublic, section::mapped_type* pmtVecStrValidators);

	void processFile(const std::string strDomain, NewcoinAddress naNodePublic, section secSite);

	bool getSeedDomains(const std::string& strDomain, seedDomain& dstSeedDomain);
	void setSeedDomains(const seedDomain& dstSeedDomain, bool bNext);

public:
	UniqueNodeList(boost::asio::io_service& io_service);

	// Begin processing.
	void start();

	void nodeAddPublic(NewcoinAddress naNodePublic, std::string strComment);
	void nodeAddDomain(std::string strDomain, validatorSource vsWhy, std::string strComment="");
	void nodeRemove(NewcoinAddress naNodePublic);
	void nodeDefault(std::string strValidators);
	void nodeReset();

	Json::Value getUnlJson();
};

#endif
// vim:ts=4
