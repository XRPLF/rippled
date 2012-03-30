#ifndef __UNIQUE_NODE_LIST__
#define __UNIQUE_NODE_LIST__

#include "../json/value.h"

#include "NewcoinAddress.h"

#include "HttpsClient.h"

#include <boost/thread/mutex.hpp>
#include <boost/container/deque.hpp>

// Guarantees minimum thoughput of 1 node per second.
#define NODE_FETCH_JOBS			10
#define NODE_FETCH_SECONDS		10
#define NODE_FILE_BYTES_MAX		(50<<10)	// 50k
#define NODE_FILE_NAME			"newcoin.txt"
#define NODE_FILE_PATH			"/" NODE_FILE_NAME

class UniqueNodeList
{
private:
	void fetchResponse(const boost::system::error_code& err, std::string strResponse);

	// hanko to public key
	//std::map<uint160, uint256> mUNL;

	boost::mutex							mFetchLock;
	int										mFetchActive;	// count of active fetches
	boost::container::deque<std::string>	mFetchPending;

	void fetchNext();
	void fetchProcess(std::string strDomain);

public:
	UniqueNodeList();

	void addNode(NewcoinAddress naNodePublic, std::string strComment);
	void fetchNode(std::string strDomain);
	void removeNode(NewcoinAddress naHanko);
	void reset();

	// 2- we don't care, 1- we care and is valid, 2-invalid signature
//	int checkValid(newcoin::Validation& valid);

	Json::Value getUnlJson();
};

#endif
// vim:ts=4
