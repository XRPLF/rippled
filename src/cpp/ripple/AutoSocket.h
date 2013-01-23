#ifndef __AUTOSOCKET_H_
#define __AUTOSOCKET_H_

#include <vector>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

// Socket wrapper that supports both SSL and non-SSL connections.
// Generally, handle it as you would an SSL connection.
// For outbound non-SSL connections, just don't call async_handshake.

namespace basio = boost::asio;
namespace bassl = basio::ssl;

class AutoSocket
{
public:
    typedef bassl::stream<basio::ip::tcp::socket>	ssl_socket;
    typedef ssl_socket::next_layer_type				plain_socket;
    typedef boost::system::error_code				error_code;
    typedef boost::function<void(error_code)> 		callback;

protected:
	ssl_socket			mSocket;
	bool				mSecure;
	callback			mCallback;

	std::vector<char>	mBuffer;

public:
	AutoSocket(basio::io_service& s, bassl::context& c) : mSocket(s, c), mSecure(false), mBuffer(4) { ; }

	bool			isSecure()		{ return mSecure; }
	ssl_socket&		SSLSocket()		{ return mSocket; }
	plain_socket&	PlainSocket()	{ return mSocket.next_layer(); }

	void async_handshake(ssl_socket::handshake_type type, callback cbFunc)
	{
		mSecure = true;
		if (type == ssl_socket::client)
			SSLSocket().async_handshake(type, cbFunc);
		else
		{
			mCallback = cbFunc;
			PlainSocket().async_receive(basio::buffer(mBuffer), basio::socket_base::message_peek,
				boost::bind(&AutoSocket::handle_autodetect, this, basio::placeholders::error));
 			            
		}
	}

	template <typename StreamType> StreamType& getSocket()
	{
		if (isSecure())
			return SSLSocket();
		if (!isSecure())
			return PlainSocket();
	}
	
	template <typename ShutdownHandler> void async_shutdown(ShutdownHandler handler)
	{
		if (isSecure())
			SSLSocket().async_shutdown(handler);
		else
		{
			PlainSocket().shutdown(plain_socket::shutdown_both);
			if (handler)
				mSocket.get_io_service().post(handler);
		}
	}

	template <typename Seq, typename Handler> void async_read_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			SSLSocket().async_read_some(buffers, handler);
		else
			PlainSocket().async_read_some(buffers, handler);
	}

	template <typename Seq, typename Handler> void async_write_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			SSLSocket().async_write_some(buffers, handler);
		else
			PlainSocket().async_write_some(buffers, handler);
	}

protected:
	void handle_autodetect(const error_code&);
};

#endif
