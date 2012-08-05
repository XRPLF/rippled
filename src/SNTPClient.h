#ifndef __SNTPCLIENT__
#define __SNTPCLIENT__

#include <string>
#include <map>
#include <vector>

#include <boost/thread/mutex.hpp>
#include <boost/asio.hpp>

class SNTPQuery
{
public:
	bool				mReceivedReply;
	time_t				mLocalTimeSent;
	int					mQueryNonce;

	SNTPQuery(time_t j = (time_t) -1)	: mReceivedReply(false), mLocalTimeSent(j) { ; }
};

class SNTPClient
{
public:
	typedef boost::shared_ptr<SNTPClient> pointer;

protected:
	std::map<boost::asio::ip::udp::endpoint, SNTPQuery>	mQueries;
	boost::mutex						mLock;

	boost::asio::io_service&			mIOService;
	boost::asio::ip::udp::socket		mSocket;
	boost::asio::deadline_timer			mTimer;
	boost::asio::ip::udp::resolver		mResolver;

	std::vector< std::pair<std::string, time_t> >	mServers;
	int												mOffset;
	time_t											mLastOffsetUpdate;

	std::vector<uint8_t>				mReceiveBuffer;
	boost::asio::ip::udp::endpoint		mReceiveEndpoint;

	void receivePacket(const boost::system::error_code& error, std::size_t bytes);
	void resolveComplete(const boost::system::error_code& error, boost::asio::ip::udp::resolver::iterator iterator);
	void sentPacket(boost::shared_ptr<std::string>, const boost::system::error_code&, std::size_t);
	void timerEntry(const boost::system::error_code&);
	void sendComplete(const boost::system::error_code& error, std::size_t bytesTransferred);
	void processReply();

public:
	SNTPClient(boost::asio::io_service& service);
	void init(const std::vector<std::string>& servers);
	void addServer(const std::string& mServer);

	void queryAll();
	bool doQuery();
	bool getOffset(int& offset);
};

#endif
