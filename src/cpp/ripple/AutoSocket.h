#ifndef __AUTOSOCKET_H_
#define __AUTOSOCKET_H_

#include <vector>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "Log.h"
extern LogPartition AutoSocketPartition;

// Socket wrapper that supports both SSL and non-SSL connections.
// Generally, handle it as you would an SSL connection.
// To force a non-SSL connection, just don't call async_handshake.
// To force SSL only inbound, call setSSLOnly.

namespace basio = boost::asio;
namespace bassl = basio::ssl;

class AutoSocket
{
public:
    typedef bassl::stream<basio::ip::tcp::socket>	ssl_socket;
    typedef ssl_socket::next_layer_type				plain_socket;
    typedef ssl_socket::lowest_layer_type			lowest_layer_type;
    typedef ssl_socket::handshake_type				handshake_type;
    typedef boost::system::error_code				error_code;
    typedef boost::function<void(error_code)>		callback;

protected:
	ssl_socket			mSocket;
	bool				mSecure;

	std::vector<char>	mBuffer;

public:
	AutoSocket(basio::io_service& s, bassl::context& c) : mSocket(s, c), mSecure(false), mBuffer(4) { ; }

	bool			isSecure()		{ return mSecure; }
	ssl_socket&		SSLSocket()		{ return mSocket; }
	plain_socket&	PlainSocket()	{ return mSocket.next_layer(); }
	void setSSLOnly()				{ mBuffer.clear(); }

	lowest_layer_type& lowest_layer()	{ return mSocket.lowest_layer(); }

	void async_handshake(handshake_type type, callback cbFunc)
	{
		if ((type == ssl_socket::client) || (mBuffer.empty()))
		{
			mSecure = true;
			mSocket.async_handshake(type, cbFunc);
		}
		else
			mSocket.next_layer().async_receive(basio::buffer(mBuffer), basio::socket_base::message_peek,
				boost::bind(&AutoSocket::handle_autodetect, this, cbFunc, basio::placeholders::error));
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
			mSocket.async_shutdown(handler);
		else
		{
			lowest_layer().shutdown(plain_socket::shutdown_both);
			mSocket.get_io_service().post(boost::bind(handler, error_code()));
		}
	}

	template <typename Seq, typename Handler> void async_read_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			mSocket.async_read_some(buffers, handler);
		else
			PlainSocket().async_read_some(buffers, handler);
	}

	template <typename Seq, typename Handler> void async_write_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			mSocket.async_write_some(buffers, handler);
		else
			PlainSocket().async_write_some(buffers, handler);
	}

protected:
	void handle_autodetect(callback cbFunc, const error_code& ec)
	{
		if (ec)
		{
			Log(lsWARNING, AutoSocketPartition) << "Handle autodetect error: " << ec;
			cbFunc(ec);
		}
		else if ((mBuffer[0] < 127) && (mBuffer[0] > 31) &&
			(mBuffer[1] < 127) && (mBuffer[1] > 31) &&
			(mBuffer[2] < 127) && (mBuffer[2] > 31) &&
			(mBuffer[3] < 127) && (mBuffer[3] > 31))
		{ // not ssl
			cbFunc(ec);
		}
		else
		{ // ssl
			mSecure = true;
			mSocket.async_handshake(ssl_socket::server, cbFunc);
		}
	}
};

#endif

// vim:ts=4
