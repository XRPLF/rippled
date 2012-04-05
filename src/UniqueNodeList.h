#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__

#include <deque>

#include "../json/value.h"

#include "NewcoinAddress.h"

#include "HttpsClient.h"
#include "ParseSection.h"

#include <boost/thread/mutex.hpp>

#define SYSTEM_NAME	"newcoin"

// Guarantees minimum thoughput of 1 node per second.
#define NODE_FETCH_JOBS			10
#define NODE_FETCH_SECONDS		10
#define NODE_FILE_BYTES_MAX		(50<<10)	// 50k
#define NODE_FILE_NAME			SYSTEM_NAME ".txt"
#define NODE_FILE_PATH			"/" NODE_FILE_NAME

class UniqueNodeList
{
private:
	void responseFetch(const std::string strDomain, const boost::system::error_code& err, const std::string strSiteFile);

	boost::mutex							mFetchLock;
	int										mFetchActive;	// count of active fetches
	std::deque<std::string>					mFetchPending;

	std::string								mStrIpsUrl;
	std::string								mStrValidatorsUrl;

	void fetchNext();
	void fetchProcess(std::string strDomain);

	void getValidatorsUrl();
	void getIpsUrl();
	void getFinish();
	void responseIps(const boost::system::error_code& err, const std::string strIpsFile);
	void responseValidators(const boost::system::error_code& err, const std::string strValidatorsFile);

	void processIps(section::mapped_type& vecStrIps);
	void processValidators(section::mapped_type& vecStrValidators);

public:
	UniqueNodeList();

	void nodeAdd(NewcoinAddress naNodePublic, std::string strComment);
	void nodeFetch(std::string strDomain);
	void nodeRemove(NewcoinAddress naHanko);
	void nodeDefault(std::string strValidators);
	void nodeReset();

	// 2- we don't care, 1- we care and is valid, 2-invalid signature
//	int checkValid(newcoin::Validation& valid);

	Json::Value getUnlJson();
};

#endif
// vim:ts=4
