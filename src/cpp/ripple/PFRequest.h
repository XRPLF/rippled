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

class PFRequest
{
public:
	typedef boost::weak_ptr<PFRequest>		wptr;
	typedef boost::shared_ptr<PFRequest>	pointer;
	typedef const pointer&					ref;
	typedef std::pair<uint160, uint160>		currIssuer_t;


protected:
	boost::weak_ptr<InfoSub>		wpSubscriber;				// Who this request came from
	Json::Value						jvStatus;					// Last result

	// Client request parameters
	RippleAddress 					raSrcAccount;
	RippleAddress					raDstAccount;
	STAmount						saDstAmount;
	std::set<currIssuer_t>			sciSourceCurrencies;
	std::vector<Json::Value>		vjvBridges;

	// Track all requests
	static std::set<wptr>			sRequests;
	static boost::recursive_mutex	sLock;

public:

	PFRequest(const boost::shared_ptr<InfoSub>& subscriber, Json::Value request);
	~PFRequest();

	Json::Value	create(const Json::Value&);
	Json::Value	close(const Json::Value&);
	Json::Value	status(const Json::Value&);
	void		update();

	static void	updateAll(const boost::shared_ptr<Ledger> &);
};

#endif
