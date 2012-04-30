
#include "ConnectionPool.h"
#include "Config.h"
#include "Peer.h"
#include "Application.h"
#include "utils.h"

#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>

ConnectionPool::ConnectionPool(boost::asio::io_service& io_service) :
	mCtx(boost::asio::ssl::context::sslv23),
	bScanning(false),
	mScanTimer(io_service)
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
	// XXX Start running policy.

	// Start scanning.
	scanRefresh();
}

// XXX Broken: also don't send a message to a peer if we got it from the peer.
void ConnectionPool::relayMessage(Peer* fromPeer, PackedMessage::pointer msg)
{
	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		Peer::pointer peer	= pair.second;

		if(!fromPeer || !(peer.get() == fromPeer))
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
		std::cerr << "ConnectionPool::connectTo: Connectting: "
			<< strIp << " " << iPort << std::endl;

		Peer::pointer peer(Peer::create(theApp->getIOService(), mCtx));

		mIpMap[ip]	= peer;

		peer->connect(strIp, iPort);

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
    Json::Value ret(Json::arrayValue);

	BOOST_FOREACH(naPeer pair, mConnectedMap)
	{
		Peer::pointer peer	= pair.second;

		ret.append(peer->getJson());
    }

    return ret;
}

// Now know peer's node public key.  Determine if we want to stay connected.
bool ConnectionPool::peerConnected(Peer::pointer peer, const NewcoinAddress& na)
{
	bool	bSuccess;

	std::cerr << "ConnectionPool::peerConnected: " << na.humanNodePublic() << std::endl;

	if (na == theApp->getWallet().getNodePublic())
	{
		std::cerr << "ConnectionPool::peerConnected: To self." << std::endl;
		bSuccess			= false;
	}
	else
	{
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
	if (bScanning && !mScanIp.compare(strIp), mScanPort == iPort)
	{
		bScanning	= false;
		scanRefresh();
	}
}

void ConnectionPool::peerVerified(const std::string& strIp, int iPort)
{
	if (bScanning && !mScanIp.compare(strIp), mScanPort == iPort)
	{
		// Scan completed successfully.
		{
			ScopedLock sl(theApp->getWalletDB()->getDBLock());
			Database *db=theApp->getWalletDB()->getDB();

			db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=NULL,ScanInterval=0 WHERE IP=%s AND Port=%d;")
				% db->escape(strIp)
				% iPort));
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
		std::string					strIp;
		int							iPort;
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

				db->getStr("IP", strIp);
				iPort		= db->getInt("Port");
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
			(void) mScanTimer.cancel();

			std::cerr << "scanRefresh: scanning: " << strIp << " " << iPort << std::endl;
			bScanning	= true;
			mScanIp		= strIp;
			mScanPort	= iPort;

			iInterval	*= 2;
			iInterval	= MAX(iInterval, theConfig.PEER_SCAN_INTERVAL_MIN);

			tpNext		= tpNow + boost::posix_time::seconds(iInterval);

			{
				ScopedLock sl(theApp->getWalletDB()->getDBLock());
				Database *db=theApp->getWalletDB()->getDB();

				db->executeSQL(str(boost::format("UPDATE PeerIps SET ScanNext=%d,ScanInterval=%d WHERE IP=%s AND Port=%d;")
					% iToSeconds(tpNext)
					% iInterval
					% db->escape(strIp)
					% iPort));
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
