#include "SNTPClient.h"

#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

#include "utils.h"
#include "Log.h"

static uint8_t SNTPQueryData[48] = {
	0x1B,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

SNTPClient::SNTPClient(boost::asio::io_service& service) :
		mIOService(service), mSocket(service), mTimer(service), mResolver(service),
		mOffset(0), mLastOffsetUpdate((time_t) -1), mReceiveBuffer(256)
{
	mSocket.open(boost::asio::ip::udp::v4());
	mSocket.async_receive_from(boost::asio::buffer(mReceiveBuffer, 256), mReceiveEndpoint,
		boost::bind(&SNTPClient::receivePacket, this, boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
	
	mTimer.expires_from_now(boost::posix_time::seconds(1));
	mTimer.async_wait(boost::bind(&SNTPClient::timerEntry, this, boost::asio::placeholders::error));
}

void SNTPClient::resolveComplete(const boost::system::error_code& error, boost::asio::ip::udp::resolver::iterator it)
{
	if (!error)
	{
		boost::asio::ip::udp::resolver::iterator sel = it;
		int i = 1;
		while (++it != boost::asio::ip::udp::resolver::iterator())
			if ((rand() % ++i) == 0)
				sel = it;
		if (sel != boost::asio::ip::udp::resolver::iterator())
		{
			boost::mutex::scoped_lock sl(mLock);
			SNTPQuery& query = mQueries[*sel];
			time_t now = time(NULL);
			if ((query.mLocalTimeSent == now) || ((query.mLocalTimeSent + 1) == now))
			{
				Log(lsINFO) << "SNTP: Redundant query suppressed";
				return;
			}
			query.mReceivedReply = false;
			query.mLocalTimeSent = now;
			mSocket.async_send_to(boost::asio::buffer(SNTPQueryData, 256), *sel,
				boost::bind(&SNTPClient::sendComplete, this,
					boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
		}
	}
}

void SNTPClient::receivePacket(const boost::system::error_code& error, std::size_t bytes_xferd)
{
	if (!error)
	{
		boost::mutex::scoped_lock sl(mLock);
		Log(lsINFO) << "SNTP: Packet from " << mReceiveEndpoint;
		std::map<boost::asio::ip::udp::endpoint, SNTPQuery>::iterator query = mQueries.find(mReceiveEndpoint);
		if (query == mQueries.end())
			Log(lsINFO) << "SNTP: Reply found without matching query";
		else if (query->second.mReceivedReply)
			Log(lsINFO) << "SNTP: Duplicate response to query";
		else
		{
			query->second.mReceivedReply = true;
			if (time(NULL) > (query->second.mLocalTimeSent + 1))
				Log(lsINFO) << "SNTP: Late response";
			else
				if (bytes_xferd < 48)
					Log(lsINFO) << "SNTP: Short reply (" << bytes_xferd << ") " << mReceiveBuffer.size();
				else
					processReply();
		}
	}

	mSocket.async_receive_from(boost::asio::buffer(mReceiveBuffer, 256), mReceiveEndpoint,
		boost::bind(&SNTPClient::receivePacket, this, boost::asio::placeholders::error,
			boost::asio::placeholders::bytes_transferred));
}

void SNTPClient::sendComplete(const boost::system::error_code& error, std::size_t)
{
	if (error)
		Log(lsINFO) << "SNTP: Send error";
}

void SNTPClient::processReply()
{
	assert(mReceiveBuffer.size() >= 48);
	uint32 *recvBuffer = reinterpret_cast<uint32*>(&mReceiveBuffer.front());

	unsigned info = ntohl(recvBuffer[0]);
	int64_t timev = ntohl(recvBuffer[8]);
	unsigned stratum = (info >> 16) & 0xff;

	if ((info >> 30) == 3)
	{
		Log(lsINFO) << "SNTP: Alarm condition";
		return;
	}
	if ((stratum == 0) || (stratum > 14))
	{
		Log(lsINFO) << "SNTP: Unreasonable stratum";
		return;
	}

	timev -= time(NULL);
	timev -= 0x83AA7E80ULL;
	Log(lsINFO) << "SNTP: Offset is " << timev;	

	// WRITEME
}

void SNTPClient::timerEntry(const boost::system::error_code& error)
{
	if (!error)
	{
		doQuery();
		mTimer.expires_from_now(boost::posix_time::seconds(10));
		mTimer.async_wait(boost::bind(&SNTPClient::timerEntry, this, boost::asio::placeholders::error));
	}
}

void SNTPClient::addServer(const std::string& server)
{
	boost::mutex::scoped_lock sl(mLock);
	mServers.push_back(std::make_pair(server, (time_t) -1));
}

void SNTPClient::init(const std::vector<std::string>& servers)
{
	std::vector<std::string>::const_iterator it = servers.begin();
	if (it == servers.end())
	{
		Log(lsINFO) << "SNTP: no server specified";
		return;
	}
	do
		addServer(*it++);
	while (it != servers.end());
	queryAll();
}

void SNTPClient::queryAll()
{
	while(doQuery())
		nothing();
}

bool SNTPClient::doQuery()
{
	boost::mutex::scoped_lock sl(mLock);
	std::vector< std::pair<std::string, time_t> >::iterator best = mServers.end();
	for (std::vector< std::pair<std::string, time_t> >::iterator it = mServers.begin(), end = best;
			it != end; ++it)
		if ((best == end) || (it->second == (time_t) -1) || (it->second < best->second))
			best = it;
	if (best == mServers.end())
	{
		Log(lsINFO) << "SNTP: No server to query";
		return false;
	}
	time_t now = time(NULL);
	if ((best->second == now) || (best->second == (now - 1)))
	{
		Log(lsINFO) << "SNTP: All servers recently queried";
		return false;
	}
	best->second = now;

	boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), best->first, "ntp");
	mResolver.async_resolve(query,
		boost::bind(&SNTPClient::resolveComplete, this,
		boost::asio::placeholders::error, boost::asio::placeholders::iterator));
	Log(lsINFO) << "SNTP: Resolve pending for " << best->first;
	return true;
}
