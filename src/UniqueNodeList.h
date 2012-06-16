#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__

#include <deque>

#include "../json/value.h"

#include "NewcoinAddress.h"
#include "Config.h"
#include "HttpsClient.h"
#include "ParseSection.h"

#include <boost/thread/mutex.hpp>
#include <boost/unordered_map.hpp>
#include <boost/unordered_set.hpp>

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

	typedef long score;

private:
	// Misc persistent information
	boost::posix_time::ptime		mtpScoreUpdated;
	boost::posix_time::ptime		mtpFetchUpdated;

	boost::recursive_mutex				mUNLLock;
	// XXX Make this faster, make this the contents vector unsigned char or raw public key.
	// XXX Contents needs to based on score.
	boost::unordered_set<std::string>	mUNL;

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
		NewcoinAddress				naPublicKey;
		validatorSource				vsSource;
		boost::posix_time::ptime	tpNext;
		boost::posix_time::ptime	tpScan;
		boost::posix_time::ptime	tpFetch;
		uint256						iSha256;
		std::string					strComment;
	} seedNode;

	// Used to distribute scores.
	typedef struct {
		int					iScore;
		int					iRoundScore;
		int					iRoundSeed;
		int					iSeen;
		std::string			strValidator;	// The public key.
		std::vector<int>	viReferrals;
	} scoreNode;

	typedef boost::unordered_map<std::string,int> strIndex;
	typedef std::pair<std::string,int> ipPort;
	typedef boost::unordered_map<std::pair< std::string, int>, score>	epScore;

	void trustedLoad();

	bool scoreRound(std::vector<scoreNode>& vsnNodes);
	int iSourceScore(validatorSource vsWhy);

	void responseFetch(const std::string strDomain, const boost::system::error_code& err, const std::string strSiteFile);

	boost::posix_time::ptime		mtpScoreNext;		// When to start scoring.
	boost::posix_time::ptime		mtpScoreStart;		// Time currently started scoring.
	boost::asio::deadline_timer		mdtScoreTimer;		// Timer to start scoring.

	void scoreNext(bool bNow);							// Update scoring timer.
	void scoreCompute();
	void scoreTimerHandler(const boost::system::error_code& err);

	boost::mutex					mFetchLock;
	int								mFetchActive;		// Count of active fetches.

	boost::posix_time::ptime		mtpFetchNext;		// Time of to start next fetch.
	boost::asio::deadline_timer		mdtFetchTimer;		// Timer to start fetching.

	void fetchNext();
	void fetchDirty();
	void fetchFinish();
	void fetchProcess(std::string strDomain);
	void fetchTimerHandler(const boost::system::error_code& err);

	void getValidatorsUrl(const NewcoinAddress& naNodePublic, section secSite);
	void getIpsUrl(const NewcoinAddress& naNodePublic, section secSite);
	void responseIps(const std::string& strSite, const NewcoinAddress& naNodePublic, const boost::system::error_code& err, const std::string strIpsFile);
	void responseValidators(const std::string& strValidatorsUrl, const NewcoinAddress& naNodePublic, section secSite, const std::string& strSite, const boost::system::error_code& err, const std::string strValidatorsFile);

	void processIps(const std::string& strSite, const NewcoinAddress& naNodePublic, section::mapped_type* pmtVecStrIps);
	void processValidators(const std::string& strSite, const std::string& strValidatorsSrc, const NewcoinAddress& naNodePublic, section::mapped_type* pmtVecStrValidators);

	void processFile(const std::string strDomain, const NewcoinAddress& naNodePublic, section secSite);

	bool getSeedDomains(const std::string& strDomain, seedDomain& dstSeedDomain);
	void setSeedDomains(const seedDomain& dstSeedDomain, bool bNext);

	bool getSeedNodes(const NewcoinAddress& naNodePublic, seedNode& dstSeedNode);
	void setSeedNodes(const seedNode& snSource, bool bNext);

	void validatorsResponse(const boost::system::error_code& err, std::string strResponse);

public:
	UniqueNodeList(boost::asio::io_service& io_service);

	// Begin processing.
	void start();

	void nodeAddPublic(const NewcoinAddress& naNodePublic, validatorSource vsWhy, const std::string& strComment);
	void nodeAddDomain(std::string strDomain, validatorSource vsWhy, const std::string& strComment="");
	void nodeRemovePublic(const NewcoinAddress& naNodePublic);
	void nodeRemoveDomain(std::string strDomain);
	void nodeDefault(const std::string& strValidators);
	void nodeReset();

	void nodeScore();

	bool nodeInUNL(const NewcoinAddress& naNodePublic);

	void nodeBootstrap();
	bool nodeLoad();
	void nodeNetwork();

	Json::Value getUnlJson();
};

#endif
// vim:ts=4
