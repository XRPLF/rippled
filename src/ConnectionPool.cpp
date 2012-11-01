
#include "ConnectionPool.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <algorithm>

#include "Config.h"
#include "Peer.h"
#include "Application.h"
#include "utils.h"
#include "Log.h"

SETUP_LOG();

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
	mLastPeer(0),
	mCtx(boost::asio::ssl::context::sslv23),
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
	if (theConfig.RUN_STANDALONE)
		return;

	// Start running policy.
	policyEnforce();

	// Start scanning.
	scanRefresh();
}

bool ConnectionPool::getTopNAddrs(int n,std::vector<std::string>& addrs)
{
	// XXX Filter out other local addresses (like ipv6)
	Database*	db = theApp->getWalletDB()->getDB();
	ScopedLock	sl(theApp->getWalletDB()->getDBLock());

	SQL_FOREACH(db, str(boost::format("SELECT IpPort FROM PeerIps LIMIT %d") % n) )
	{
		std::string str;

		db->getStr(0,str);

		addrs.push_back(str);
	}

	return true;
}

bool ConnectionPool::savePeer(const std::string& strIp, int iPort, char code)
{
	bool	bNew	= false;

	Database* db = theApp->getWalletDB()->getDB();

	std::string ipPort	= sqlEscape(str(boost::format("%s %d") % strIp % iPort));

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	std::string sql	= str(boost::format("SELECT COUNT(*) FROM PeerIps WHERE IpPort=%s;") % ipPort);
	if (db->executeSQL(sql) && db->startIterRows())
	{
		if (!db->getInt(0))
		{
			db->executeSQL(str(boost::format("INSERT INTO PeerIps (IpPort,Score,Source) values (%s,0,'%c');") % ipPort % code));
			bNew	= true;
		}
		else
		{
			// We already had this peer.
			// We will eventually verify its address if it is possible.
			// YYY If it is vsInbound, then we might make verification immediate so we can connect back sooner if the connection
			// is lost.
			nothing();
		}
	}
	else
	{
		std::cout << "Error saving Peer" << std::endl;
	}

	if (bNew)
		scanRefresh();

	return bNew;
}

// An available peer is one we had no trouble connect to last time and that we are not currently knowingly connected or connecting
// too.
//
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

			vstrIpPort.push_back(sqlEscape(str(boost::format("%s %d") % strIp % iPort)));
		}
	}

	// Get the first IpPort entry which is not in vector and which is not scheduled for scanning.
	std::string strIpPort;

	{
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		if (db->executeSQL(str(boost::format("SELECT IpPort FROM PeerIps WHERE ScanNext IS NULL AND IpPort NOT IN (%s) LIMIT 1;")
			% strJoin(vstrIpPort.begin(), vstrIpPort.end(), ",")))
			&& db->startIterRows())
		{
			strIpPort	= db->getStrBinary("IpPort");
		}
	}

	bool		bAvailable	= !strIpPort.empty();

	if (bAvailable)
		splitIpPort(strIpPort, strIp, iPort);

	return bAvailable;
}

// Make sure we have at least low water connections.
void ConnectionPool::policyLowWater()
{
	std::string	strIp;
	int			iPort;

	// Find an entry to connect to.
	if (mConnectedMap.size() > theConfig.PEER_CONNECT_LOW_WATER)
	{
		// Above low water mark, don't need more connections.
		cLog(lsTRACE) << "Pool: Low water: sufficient connections: " << mConnectedMap.size() << "/" << theConfig.PEER_CONNECT_LOW_WATER;

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
		cLog(lsTRACE) << "Pool: Low water: no peers available.";

		// XXX Might ask peers for more ips.
		nothing();
	}
	else
	{
		// Try to start connection.
		cLog(lsTRACE) << "Pool: Low water: start connection.";

		if (!peerConnect(strIp, iPort))
		{
			cLog(lsINFO) << "Pool: Low water: already connected.";
		}

		// Check if we need more.
		policyLowWater();
	}
}

void ConnectionPool::policyEnforce()
{
	// Cancel any in progress timer.
	(void) mPolicyTimer.cancel();

	// Enforce policies.
	policyLowWater();

	// Schedule next enforcement.
	mPolicyTimer.expires_at(boost::posix_time::second_clock::universal_time()+boost::posix_time::seconds(POLICY_INTERVAL_SECONDS));
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

// YYY: Should probably do this in the background.
// YYY: Might end up sending to disconnected peer?
int ConnectionPool::relayMessage(Peer* fromPeer, const PackedMessage::pointer& msg)
{
	int sentTo = 0;
	boost::mutex::scoped_lock sl(mPeerLock);

	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		Peer::ref peer	= pair.second;
		if (!peer)
			std::cerr << "CP::RM null peer in list" << std::endl;
		else if ((!fromPeer || !(peer.get() == fromPeer)) && peer->isConnected())
		{
			++sentTo;
			peer->sendPacket(msg);
		}
	}

	return sentTo;
}

void ConnectionPool::relayMessageBut(const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg)
{ // Relay message to all but the specified peers
	boost::mutex::scoped_lock sl(mPeerLock);

	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		Peer::ref peer	= pair.second;
		if (peer->isConnected() && (fromPeers.count(peer->getPeerId()) == 0))
			peer->sendPacket(msg);
	}

}

void ConnectionPool::relayMessageTo(const std::set<uint64>& fromPeers, const PackedMessage::pointer& msg)
{ // Relay message to the specified peers
	boost::mutex::scoped_lock sl(mPeerLock);

	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		Peer::ref peer	= pair.second;
		if (peer->isConnected() && (fromPeers.count(peer->getPeerId()) > 0))
			peer->sendPacket(msg);
	}

}

// Schedule a connection via scanning.
//
// Add or modify into PeerIps as a manual entry for immediate scanning.
// Requires sane IP and port.
void ConnectionPool::connectTo(const std::string& strIp, int iPort)
{
	if (theConfig.RUN_STANDALONE)
		return;
	{
		Database*	db	= theApp->getWalletDB()->getDB();
		ScopedLock	sl(theApp->getWalletDB()->getDBLock());

		db->executeSQL(str(boost::format("REPLACE INTO PeerIps (IpPort,Score,Source,ScanNext) values (%s,%d,'%c',0);")
			% sqlEscape(str(boost::format("%s %d") % strIp % iPort))
			% theApp->getUNL().iSourceScore(UniqueNodeList::vsManual)
			% char(UniqueNodeList::vsManual)));
	}

	scanRefresh();
}

// Start a connection, if not already known connected or connecting.
//
// <-- true, if already connected.
Peer::pointer ConnectionPool::peerConnect(const std::string& strIp, int iPort)
{
	ipPort			pipPeer		= make_pair(strIp, iPort);
	Peer::pointer	ppResult	= Peer::pointer();

    boost::unordered_map<ipPort, Peer::pointer>::iterator	it;

	{
		boost::mutex::scoped_lock sl(mPeerLock);

		if ((it = mIpMap.find(pipPeer)) == mIpMap.end())
		{
			Peer::pointer	ppNew(Peer::create(theApp->getIOService(), mCtx, ++mLastPeer));

			// Did not find it.  Not already connecting or connected.
			ppNew->connect(strIp, iPort);

			mIpMap[pipPeer]	= ppNew;

			ppResult		= ppNew;
			// ++miConnectStarting;
		}
		else
		{
			// Found it.  Already connected.

			nothing();
		}
	}

	if (ppResult)
	{
		//cLog(lsINFO) << "Pool: Connecting: " << ADDRESS_SHARED(ppResult) << ": " << strIp << " " << iPort;
	}
	else
	{
		//cLog(lsINFO) << "Pool: Already connected: " << strIp << " " << iPort;
	}

	return ppResult;
}

// Returns information on verified peers.
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

int ConnectionPool::getPeerCount()
{
	boost::mutex::scoped_lock sl(mPeerLock);

	return mConnectedMap.size();
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

uint64 ConnectionPool::assignPeerId()
{
	boost::mutex::scoped_lock sl(mPeerLock);
	return ++mLastPeer;
}

// Now know peer's node public key.  Determine if we want to stay connected.
// <-- bNew: false = redundant
bool ConnectionPool::peerConnected(Peer::ref peer, const RippleAddress& naPeer,
	const std::string& strIP, int iPort)
{
	bool	bNew	= false;

	assert(!!peer);

	if (naPeer == theApp->getWallet().getNodePublic())
	{
		cLog(lsINFO) << "Pool: Connected: self: " << ADDRESS_SHARED(peer) << ": " << naPeer.humanNodePublic() << " " << strIP << " " << iPort;
	}
	else
	{
		boost::mutex::scoped_lock sl(mPeerLock);
		boost::unordered_map<RippleAddress, Peer::pointer>::iterator itCm	= mConnectedMap.find(naPeer);

		if (itCm == mConnectedMap.end())
		{
			// New connection.
			//cLog(lsINFO) << "Pool: Connected: new: " << ADDRESS_SHARED(peer) << ": " << naPeer.humanNodePublic() << " " << strIP << " " << iPort;

			mConnectedMap[naPeer]	= peer;
			bNew					= true;
		}
		// Found in map, already connected.
		else if (!strIP.empty())
		{
			// Was an outbound connection, we know IP and port.
			// Note in previous connection how to reconnect.
			if (itCm->second->getIP().empty())
			{
				// Old peer did not know it's IP.
				//cLog(lsINFO) << "Pool: Connected: redundant: outbound: " << ADDRESS_SHARED(peer) << " discovered: " << ADDRESS_SHARED(itCm->second) << ": " << strIP << " " << iPort;

				itCm->second->setIpPort(strIP, iPort);

				// Add old connection to identified connection list.
				mIpMap[make_pair(strIP, iPort)]	= itCm->second;
			}
			else
			{
				// Old peer knew its IP.  Do nothing.
				//cLog(lsINFO) << "Pool: Connected: redundant: outbound: rediscovered: " << ADDRESS_SHARED(peer) << " " << strIP << " " << iPort;

				nothing();
			}
		}
		else
		{
			//cLog(lsINFO) << "Pool: Connected: redundant: inbound: " << ADDRESS_SHARED(peer) << " " << strIP << " " << iPort;

			nothing();
		}
	}

	return bNew;
}

// We maintain a map of public key to peer for connected and verified peers.  Maintain it.
void ConnectionPool::peerDisconnected(Peer::ref peer, const RippleAddress& naPeer)
{
	if (naPeer.isValid())
	{
		boost::unordered_map<RippleAddress, Peer::pointer>::iterator itCm;

		boost::mutex::scoped_lock sl(mPeerLock);

		itCm	= mConnectedMap.find(naPeer);

		if (itCm == mConnectedMap.end())
		{
			// Did not find it.  Not already connecting or connected.
			cLog(lsWARNING) << "Pool: disconnected: Internal Error: mConnectedMap was inconsistent.";
			// XXX Maybe bad error, considering we have racing connections, may not so bad.
		}
		else if (itCm->second != peer)
		{
			cLog(lsWARNING) << "Pool: disconected: non canonical entry";

			nothing();
		}
		else
		{
			// Found it. Delete it.
			mConnectedMap.erase(itCm);

			//cLog(lsINFO) << "Pool: disconnected: " << naPeer.humanNodePublic() << " " << peer->getIP() << " " << peer->getPort();
		}
	}
	else
	{
		//cLog(lsINFO) << "Pool: disconnected: anonymous: " << peer->getIP() << " " << peer->getPort();
	}
}

// Schedule for immediate scanning, if not already scheduled.
//
// <-- true, scanRefresh needed.
bool ConnectionPool::peerScanSet(const std::string& strIp, int iPort)
{
	std::string	strIpPort	= str(boost::format("%s %d") % strIp % iPort);
	bool		bScanDirty	= false;

	ScopedLock	sl(theApp->getWalletDB()->getDBLock());
	Database*	db = theApp->getWalletDB()->getDB();

	if (db->executeSQL(str(boost::format("SELECT ScanNext FROM PeerIps WHERE IpPort=%s;")
		% sqlEscape(strIpPort)))
		&& db->startIterRows())
	{
		if (db->getNull("ScanNext"))
		{
			// Non-scanning connection terminated.  Schedule for scanning.
			int							iInterval	= theConfig.PEER_SCAN_INTERVAL_MIN;
			boost::posix_time::ptime	tpNow		= boost::posix_time::second_clock::universal_time();
			boost::posix_time::ptime	tpNext		= tpNow + boost::posix_time::seconds(iInterval);

			//cLog(lsINFO) << str(boost::format("Pool: Scan: schedule create: %s %s (next %s, delay=%d)")
			//	% mScanIp % mScanPort % tpNext % (tpNext-tpNow).total_seconds());

			db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=%d,ScanInterval=%d WHERE IpPort=%s;")
				% iToSeconds(tpNext)
				% iInterval
				% db->escape(strIpPort)));

			bScanDirty	= true;
		}
		else
		{
			// Scan connection terminated, already scheduled for retry.
			// boost::posix_time::ptime	tpNow		= boost::posix_time::second_clock::universal_time();
			// boost::posix_time::ptime	tpNext		= ptFromSeconds(db->getInt("ScanNext"));

			//cLog(lsINFO) << str(boost::format("Pool: Scan: schedule exists: %s %s (next %s, delay=%d)")
			//	% mScanIp % mScanPort % tpNext % (tpNext-tpNow).total_seconds());
		}
	}
	else
	{
		//cLog(lsWARNING) << "Pool: Scan: peer wasn't in PeerIps: " << strIp << " " << iPort;
	}

	return bScanDirty;
}

// --> strIp: not empty
void ConnectionPool::peerClosed(Peer::ref peer, const std::string& strIp, int iPort)
{
	ipPort		ipPeer			= make_pair(strIp, iPort);
	bool		bScanRefresh	= false;

	// If the connection was our scan, we are no longer scanning.
	if (mScanning && mScanning == peer)
	{
		//cLog(lsINFO) << "Pool: Scan: scan fail: " << strIp << " " << iPort;

		mScanning		= Peer::pointer();	// No longer scanning.
		bScanRefresh	= true;				// Look for more to scan.
	}

	// Determine if closed peer was redundant.
	bool	bRedundant	= true;
	{
		boost::mutex::scoped_lock sl(mPeerLock);
		boost::unordered_map<ipPort, Peer::pointer>::iterator	itIp;

		itIp	= mIpMap.find(ipPeer);

		if (itIp == mIpMap.end())
		{
			// Did not find it.  Not already connecting or connected.
			cLog(lsWARNING) << "Pool: Closed: UNEXPECTED: " << ADDRESS_SHARED(peer) << ": " << strIp << " " << iPort;
			// XXX Internal error.
		}
		else if (mIpMap[ipPeer] == peer)
		{
			// We were the identified connection.
			//cLog(lsINFO) << "Pool: Closed: identified: " << ADDRESS_SHARED(peer) << ": " << strIp << " " << iPort;

			// Delete our entry.
			mIpMap.erase(itIp);

			bRedundant	= false;
		}
		else
		{
			// Found it.  But, we were redundant.
			//cLog(lsINFO) << "Pool: Closed: redundant: " << ADDRESS_SHARED(peer) << ": " << strIp << " " << iPort;
		}
	}

	if (!bRedundant)
	{
		// If closed was not redundant schedule if not already scheduled.
		bScanRefresh	= peerScanSet(ipPeer.first, ipPeer.second) || bScanRefresh;
	}

	if (bScanRefresh)
		scanRefresh();
}

void ConnectionPool::peerVerified(Peer::ref peer)
{
	if (mScanning && mScanning == peer)
	{
		// Scan completed successfully.
		std::string	strIp	= peer->getIP();
		int			iPort	= peer->getPort();

		std::string	strIpPort	= str(boost::format("%s %d") % strIp % iPort);

		//cLog(lsINFO) << str(boost::format("Pool: Scan: connected: %s %s %s (scanned)") % ADDRESS_SHARED(peer) % strIp % iPort);

		if (peer->getNodePublic() == theApp->getWallet().getNodePublic())
		{
			// Talking to ourself.  We will just back off.  This lets us maybe advertise our outside address.

			nothing();	// Do nothing, leave scheduled scanning.
		}
		else
		{
			// Talking with a different peer.
			ScopedLock sl(theApp->getWalletDB()->getDBLock());
			Database *db=theApp->getWalletDB()->getDB();

			db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=NULL,ScanInterval=0 WHERE IpPort=%s;")
				% db->escape(strIpPort)));
			// XXX Check error.
		}

		mScanning	= Peer::pointer();

		scanRefresh();	// Continue scanning.
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
	if (mScanning)
	{
		// Currently scanning, will scan again after completion.
		cLog(lsTRACE) << "Pool: Scan: already scanning";

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
			ScopedLock	sl(theApp->getWalletDB()->getDBLock());
			Database*	db = theApp->getWalletDB()->getDB();

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
			//cLog(lsINFO) << "Pool: Scan: stop.";

			(void) mScanTimer.cancel();
		}
		else if (tpNext <= tpNow)
		{
			// Scan it.
			splitIpPort(strIpPort, mScanIp, mScanPort);

			(void) mScanTimer.cancel();

			iInterval	= std::max(iInterval, theConfig.PEER_SCAN_INTERVAL_MIN);

			tpNext		= tpNow + boost::posix_time::seconds(iInterval);

			//cLog(lsINFO) << str(boost::format("Pool: Scan: Now: %s %s (next %s, delay=%d)")
			//	% mScanIp % mScanPort % tpNext % (tpNext-tpNow).total_seconds());

			iInterval	*= 2;

			{
				ScopedLock sl(theApp->getWalletDB()->getDBLock());
				Database *db=theApp->getWalletDB()->getDB();

				db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=%d,ScanInterval=%d WHERE IpPort=%s;")
					% iToSeconds(tpNext)
					% iInterval
					% db->escape(strIpPort)));
				// XXX Check error.
			}

			mScanning	= peerConnect(mScanIp, mScanPort);
			if (!mScanning)
			{
				// Already connected. Try again.
				scanRefresh();
			}
		}
		else
		{
			//cLog(lsINFO) << str(boost::format("Pool: Scan: Next: %s (next %s, delay=%d)")
			//	% strIpPort % tpNext % (tpNext-tpNow).total_seconds());

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
