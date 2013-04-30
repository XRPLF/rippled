#ifndef _PFREQUEST__H
#define _PFREQUEST__H

#include <set>
#include <vector>

#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>

#include "../json/value.h"

#include "uint256.h"
#include "RippleAddress.h"
#include "SerializedTypes.h"

// A pathfinding request submitted by a client
// The request issuer must maintain a strong pointer

class Ledger;
class InfoSub;
class STAmount;

// Return values from parseJson
#define PFR_PJ_COMPLETE				0
#define PFR_PJ_NOCHANGE				1
#define PFR_PJ_CHANGE				2
#define PFR_PJ_INVALID				3

class PFRequest
{
public:
	typedef boost::weak_ptr<PFRequest>		wptr;
	typedef boost::shared_ptr<PFRequest>	pointer;
	typedef const pointer&					ref;
	typedef std::pair<uint160, uint160>		currIssuer_t;


protected:
	boost::recursive_mutex			mLock;
	boost::weak_ptr<InfoSub>		wpSubscriber;				// Who this request came from
	Json::Value						jvStatus;					// Last result

	// Client request parameters
	RippleAddress 					raSrcAccount;
	RippleAddress					raDstAccount;
	STAmount						saDstAmount;
	std::set<currIssuer_t>			sciSourceCurrencies;
	std::vector<Json::Value>		vjvBridges;

	bool							bValid;

	// Track all requests
	static std::set<wptr>			sRequests;
	static boost::recursive_mutex	sLock;

	int parseJson(const Json::Value&);

public:

	PFRequest(const boost::shared_ptr<InfoSub>& subscriber, Json::Value request);

	bool		isValid();
	Json::Value	getStatus();

	Json::Value	doCreate(const Json::Value&);
	Json::Value	doClose(const Json::Value&);
	Json::Value	doStatus(const Json::Value&);

	void		doUpdate();

	static void	updateAll(const boost::shared_ptr<Ledger> &);
};

#endif
