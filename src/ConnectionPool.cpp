
#include "ConnectionPool.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>

#include "Config.h"
#include "Peer.h"
#include "Application.h"
#include "utils.h"
#include "Log.h"


// How often to enforce policies.
#define POLICY_INTERVAL_SECONDS	5

void splitIpPort(const std::string& strIpPort, std::string& strIp, int& iPort)
{
	std::vector<std::string>	vIpPort;
	boost::split(vIpPort, strIpPort, boost::is_any_of(" "));

	strIp	= vIpPort[0];
	iPort	= boost::lexical_cast<int>(vIpPort[1]);
}

ConnectionPool::ConnectionPool(boost::asio::io_service& io_service) :
	mCtx(boost::asio::ssl::context::sslv23),
	bScanning(false),
	mScanTimer(io_service),
	mPolicyTimer(io_service)
{
	mCtx.set_options(
		boost::asio::ssl::context::default_workarounds
		| boost::asio::ssl::context::no_sslv2
		| boost::asio::ssl::context::single_dh_use);

	if (1 != SSL_CTX_set_cipher_list(mCtx.native_handle(), theConfig.PEER_SSL_CIPHER_LIST.c_str()))
		std::runtime_error("Error setting cipher list (no valid ciphers).");
}

void ConnectionPool::start()
{
	// Start running policy.
	policyEnforce();

	// Start scanning.
	scanRefresh();
}

bool ConnectionPool::getTopNAddrs(int n,std::vector<std::string>& addrs)
{
	Database* db = theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	SQL_FOREACH(db, str(boost::format("SELECT IpPort FROM PeerIps limit %d") % n) )
	{
		std::string str;
		db->getStr(0,str);
		addrs.push_back(str);
	}

	return true;
}

bool ConnectionPool::savePeer(const std::string& strIp, int iPort,char code)
{
	Database* db = theApp->getWalletDB()->getDB();

	std::string ipPort=db->escape(str(boost::format("%s %d") % strIp % iPort));

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	std::string sql=str(boost::format("SELECT count(*) FROM PeerIps WHERE IpPort=%s;") % ipPort);
	if (db->executeSQL(sql) && db->startIterRows())
	{
		if ( db->getInt(0)==0)
		{
			db->executeSQL(str(boost::format("INSERT INTO PeerIps (IpPort,Score,Source) values (%s,0,'%c');")	% ipPort % code));
			return true;
		}// else we already had this peer
	}
	else
	{
		std::cout << "Error saving Peer" << std::endl;
	}

	return false;
}

// <-- true, if a peer is available to connect to
bool ConnectionPool::peerAvailable(std::string& strIp, int& iPort)
{
	Database*					db = theApp->getWalletDB()->getDB();
	std::vector<std::string>	vstrIpPort;

	// Convert mIpMap (list of open connections) to a vector of "<ip> <port>".
	{
		boost::mutex::scoped_lock	sl(mPeerLock);
		vstrIpPort.reserve(mIpMap.size());

		BOOST_FOREACH(pipPeer ipPeer, mIpMap)
		{
			const std::string&	strIp	= ipPeer.first.first;
			int					iPort	= ipPeer.first.second;

			vstrIpPort.push_back(db->escape(str(boost::format("%s %d") % strIp % iPort)));
		}
	}

	// Get the first IpPort entry which is not in vector and which is not scheduled for scanning.
	std::string strIpPort;

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	if (db->executeSQL(str(boost::format("SELECT IpPort FROM PeerIps WHERE ScanNext IS NULL AND IpPort NOT IN (%s) LIMIT 1;")
		% strJoin(vstrIpPort.begin(), vstrIpPort.end(), ",")))
		&& db->startIterRows())
	{
		db->getStr("IpPort", strIpPort);
	}

	bool		bAvailable	= !strIpPort.empty();

	if (bAvailable)
		splitIpPort(strIpPort, strIp, iPort);

	return bAvailable;
}

void ConnectionPool::policyLowWater()
{
	std::string	strIp;
	int			iPort;

	// Find an entry to connect to.
	if (mConnectedMap.size() > theConfig.PEER_CONNECT_LOW_WATER)
	{
		// Above low water mark, don't need more connections.
		nothing();
	}
#if 0
	else if (miConnectStarting == theConfig.PEER_START_MAX)
	{
		// Too many connections starting to start another.
		nothing();
	}
#endif
	else if (!peerAvailable(strIp, iPort))
	{
		// No more connections available to start.
		// XXX Might ask peers for more ips.
		nothing();
	}
	else
	{
		// Try to start connection.
		if (!connectTo(strIp, iPort))
			throw std::runtime_error("Internal error: standby was already connected.");

		// Check if we need more.
		policyLowWater();
	}
}

void ConnectionPool::policyEnforce()
{
	boost::posix_time::ptime	tpNow	= boost::posix_time::second_clock::universal_time();

	Log(lsTRACE) << "policyEnforce: begin: " << tpNow;

	// Cancel any in progrss timer.
	(void) mPolicyTimer.cancel();

	// Enforce policies.
	policyLowWater();

	// Schedule next enforcement.
	boost::posix_time::ptime	tpNext;

	tpNext	= boost::posix_time::second_clock::universal_time()+boost::posix_time::seconds(POLICY_INTERVAL_SECONDS);

	Log(lsTRACE) << "policyEnforce: schedule : " << tpNext;

	mPolicyTimer.expires_at(tpNext);
	mPolicyTimer.async_wait(boost::bind(&ConnectionPool::policyHandler, this, _1));
}

void ConnectionPool::policyHandler(const boost::system::error_code& ecResult)
{
	if (ecResult == boost::asio::error::operation_aborted)
	{
		nothing();
	}
	else if (!ecResult)
	{
		policyEnforce();
	}
	else
	{
		throw std::runtime_error("Internal error: unexpected deadline error.");
	}
}

// XXX Broken: also don't send a message to a peer if we got it from the peer.
void ConnectionPool::relayMessage(Peer* fromPeer, PackedMessage::pointer msg)
{
	boost::mutex::scoped_lock sl(mPeerLock);

	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		Peer::pointer peer	= pair.second;
		if (!peer)
			std::cerr << "CP::RM null peer in list" << std::endl;
		else if (!fromPeer || !(peer.get() == fromPeer))
			peer->sendPacket(msg);
	}
}

// Inbound connection, false=reject
// Reject addresses we already have in our table.
// XXX Reject, if we have too many connections.
bool ConnectionPool::peerRegister(Peer::pointer peer, const std::string& strIp, int iPort)
{
	bool	bAccept;
	ipPort	ip	= make_pair(strIp, iPort);

    boost::unordered_map<ipPort, Peer::pointer>::iterator	it;

	boost::mutex::scoped_lock sl(mPeerLock);

	it	= mIpMap.find(ip);

	if (it == mIpMap.end())
	{
		// Did not find it.  Not already connecting or connected.

		std::cerr << "ConnectionPool::peerRegister: " << ip.first << " " << ip.second << std::endl;
		// Mark as connecting.
		mIpMap[ip]	= peer;
		bAccept		= true;
	}
	else
	{
		// Found it.  Already connected or connecting.

		bAccept	= false;
	}

	return bAccept;
}

bool ConnectionPool::connectTo(const std::string& strIp, int iPort)
{
	bool	bConnecting;
	ipPort	ip	= make_pair(strIp, iPort);

    boost::unordered_map<ipPort, Peer::pointer>::iterator	it;

	boost::mutex::scoped_lock sl(mPeerLock);

	it	= mIpMap.find(ip);

	if (it == mIpMap.end())
	{
		// Did not find it.  Not already connecting or connected.
		std::cerr << "ConnectionPool::connectTo: Connecting: "
			<< strIp << " " << iPort << std::endl;

		Peer::pointer peer(Peer::create(theApp->getIOService(), mCtx));

		mIpMap[ip]	= peer;

		peer->connect(strIp, iPort);

		// ++miConnectStarting;

		bConnecting	= true;
	}
	else
	{
		// Found it.  Already connected.
		std::cerr << "ConnectionPool::connectTo: Already connected: "
			<< strIp << " " << iPort << std::endl;

		bConnecting	= false;
	}

	return bConnecting;
}

Json::Value ConnectionPool::getPeersJson()
{
    Json::Value					ret(Json::arrayValue);
	std::vector<Peer::pointer>	vppPeers	= getPeerVector();

	BOOST_FOREACH(Peer::pointer peer, vppPeers)
	{
		ret.append(peer->getJson());
    }

    return ret;
}

std::vector<Peer::pointer> ConnectionPool::getPeerVector()
{
	std::vector<Peer::pointer> ret;

	boost::mutex::scoped_lock sl(mPeerLock);

	ret.reserve(mConnectedMap.size());

	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		assert(!!pair.second);
		ret.push_back(pair.second);
    }

    return ret;
}

// Now know peer's node public key.  Determine if we want to stay connected.
bool ConnectionPool::peerConnected(Peer::pointer peer, const NewcoinAddress& na)
{
	bool	bSuccess;

	std::cerr << "ConnectionPool::peerConnected: " << na.humanNodePublic()
		<< " " << peer->getIP() << " " << peer->getPort()
		<< std::endl;
	assert(!!peer);
	if (na == theApp->getWallet().getNodePublic())
	{
		std::cerr << "ConnectionPool::peerConnected: To self." << std::endl;
		bSuccess			= false;
	}
	else
	{
		boost::mutex::scoped_lock sl(mPeerLock);

		mConnectedMap[na]	= peer;
		bSuccess			= true;
	}


	return bSuccess;
}

void ConnectionPool::peerDisconnected(Peer::pointer peer, const ipPort& ipPeer, const NewcoinAddress& naPeer)
{
	std::cerr << "ConnectionPool::peerDisconnected: " << ipPeer.first << " " << ipPeer.second << std::endl;

	boost::mutex::scoped_lock sl(mPeerLock);

	if (naPeer.isValid())
	{
		boost::unordered_map<NewcoinAddress, Peer::pointer>::iterator itCm;

		itCm	= mConnectedMap.find(naPeer);

		if (itCm == mConnectedMap.end())
		{
			// Did not find it.  Not already connecting or connected.
			std::cerr << "Internal Error: peer connection was inconsistent." << std::endl;
			// XXX Bad error.
		}
		else
		{
			// Found it. Delete it.
			mConnectedMap.erase(itCm);
		}
	}

    boost::unordered_map<ipPort, Peer::pointer>::iterator	itIp;

	itIp	= mIpMap.find(ipPeer);

	if (itIp == mIpMap.end())
	{
		// Did not find it.  Not already connecting or connected.
		std::cerr << "Internal Error: peer wasn't connected: "
			<< ipPeer.first << " " << ipPeer.second << std::endl;
		// XXX Bad error.
	}
	else
	{
		// Found it. Delete it.
		mIpMap.erase(itIp);
	}
}

void ConnectionPool::peerFailed(const std::string& strIp, int iPort)
{
	// If the fail was our scan, we are no longer scanning.
	if (bScanning && !mScanIp.compare(strIp) && mScanPort == iPort)
	{
		bScanning	= false;

		// Look for more to scan.
		scanRefresh();
	}
}

void ConnectionPool::peerVerified(const std::string& strIp, int iPort)
{
	if (bScanning && !mScanIp.compare(strIp), mScanPort == iPort)
	{
		std::string	strIpPort	= str(boost::format("%s %d") % strIp % iPort);
		// Scan completed successfully.
		{
			ScopedLock sl(theApp->getWalletDB()->getDBLock());
			Database *db=theApp->getWalletDB()->getDB();

			db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=NULL,ScanInterval=0 WHERE IpPort=%s;")
				% db->escape(strIpPort)));
			// XXX Check error.
		}

		bScanning	= false;
		scanRefresh();
	}
}

void ConnectionPool::scanHandler(const boost::system::error_code& ecResult)
{
	if (ecResult == boost::asio::error::operation_aborted)
	{
		nothing();
	}
	else if (!ecResult)
	{
		scanRefresh();
	}
	else
	{
		throw std::runtime_error("Internal error: unexpected deadline error.");
	}
}

// Scan ips as per db entries.
void ConnectionPool::scanRefresh()
{
	if (bScanning)
	{
		// Currently scanning, will scan again after completion.
		std::cerr << "scanRefresh: already scanning" << std::endl;

		nothing();
	}
	else
	{
		// Discover if there are entries that need scanning.
		boost::posix_time::ptime	tpNext;
		boost::posix_time::ptime	tpNow;
		std::string					strIpPort;
		int							iInterval;

		{
			ScopedLock sl(theApp->getWalletDB()->getDBLock());
			Database *db=theApp->getWalletDB()->getDB();

			if (db->executeSQL("SELECT * FROM PeerIps INDEXED BY PeerScanIndex WHERE ScanNext NOT NULL ORDER BY ScanNext LIMIT 1;")
				&& db->startIterRows())
			{
				// Have an entry to scan.
				int			iNext	= db->getInt("ScanNext");

				tpNext	= ptFromSeconds(iNext);
				tpNow	= boost::posix_time::second_clock::universal_time();

				db->getStr("IpPort", strIpPort);
				iInterval	= db->getInt("ScanInterval");
			}
			else
			{
				// No entries to scan.
				tpNow	= boost::posix_time::ptime(boost::posix_time::not_a_date_time);
			}
		}

		if (tpNow.is_not_a_date_time())
		{
			std::cerr << "scanRefresh: no scan needed." << std::endl;

			(void) mScanTimer.cancel();
		}
		else if (tpNext <= tpNow)
		{
			// Scan it.
			splitIpPort(strIpPort, mScanIp, mScanPort);

			(void) mScanTimer.cancel();

			std::cerr << "scanRefresh: scanning: " << mScanIp << " " << mScanPort << std::endl;
			bScanning	= true;

			iInterval	*= 2;
			iInterval	= MAX(iInterval, theConfig.PEER_SCAN_INTERVAL_MIN);

			tpNext		= tpNow + boost::posix_time::seconds(iInterval);

			{
				ScopedLock sl(theApp->getWalletDB()->getDBLock());
				Database *db=theApp->getWalletDB()->getDB();

				db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=%d,ScanInterval=%d WHERE IpPort=%s;")
					% iToSeconds(tpNext)
					% iInterval
					% db->escape(strIpPort)));
				// XXX Check error.
			}

			if (!connectTo(mScanIp, mScanPort))
			{
				// Already connected. Try again.
				scanRefresh();
			}
		}
		else
		{
			std::cerr << "scanRefresh: next due: " << tpNow << std::endl;

			mScanTimer.expires_at(tpNext);
			mScanTimer.async_wait(boost::bind(&ConnectionPool::scanHandler, this, _1));
		}
	}
}

#if 0
bool ConnectionPool::isMessageKnown(PackedMessage::pointer msg)
{
	for(unsigned int n=0; n<mBroadcastMessages.size(); n++)
	{
		if(msg==mBroadcastMessages[n].first) return(false);
	}
	return(false);
}
#endif

// vim:ts=4
