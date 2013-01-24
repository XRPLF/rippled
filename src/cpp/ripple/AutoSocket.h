#ifndef __AUTOSOCKET_H_
#define __AUTOSOCKET_H_

#include <vector>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/make_shared.hpp>
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
    typedef boost::shared_ptr<ssl_socket>			socket_ptr;
    typedef ssl_socket::next_layer_type				plain_socket;
    typedef ssl_socket::lowest_layer_type			lowest_layer_type;
    typedef ssl_socket::handshake_type				handshake_type;
    typedef boost::system::error_code				error_code;
    typedef boost::function<void(error_code)>		callback;

protected:
	socket_ptr			mSocket;
	bool				mSecure;

	std::vector<char>	mBuffer;

public:
	AutoSocket(basio::io_service& s, bassl::context& c) : mSecure(false), mBuffer(4)
	{
		mSocket = boost::make_shared<ssl_socket>(boost::ref(s), boost::ref(c));
	}

	AutoSocket(basio::io_service& s, bassl::context& c, bool secureOnly, bool plainOnly)
		: mSecure(secureOnly), mBuffer((plainOnly || secureOnly) ? 0 : 4)
	{
		mSocket = boost::make_shared<ssl_socket>(boost::ref(s), boost::ref(c));
	}

	bool			isSecure()		{ return mSecure; }
	ssl_socket&		SSLSocket()		{ return *mSocket; }
	plain_socket&	PlainSocket()	{ return mSocket->next_layer(); }
	void setSSLOnly()				{ mSecure = true;}
	void setPlainOnly()				{ mBuffer.clear(); }

	lowest_layer_type& lowest_layer()	{ return mSocket->lowest_layer(); }

	void swap(AutoSocket& s)
	{
		mBuffer.swap(s.mBuffer);
		mSocket.swap(s.mSocket);
		std::swap(mSecure, s.mSecure);
	}

	void async_handshake(handshake_type type, callback cbFunc)
	{
		if ((type == ssl_socket::client) || (mSecure))
		{ // must be ssl
			mSecure = true;
			mSocket->async_handshake(type, cbFunc);
		}
		else if (mBuffer.empty())
		{ // must be plain
			mSecure = false;
			mSocket->get_io_service().post(boost::bind(cbFunc, error_code()));
		}
		else
		{ // autodetect
			mSocket->next_layer().async_receive(basio::buffer(mBuffer), basio::socket_base::message_peek,
				boost::bind(&AutoSocket::handle_autodetect, this, cbFunc, basio::placeholders::error));
		}
	}

	template <typename ShutdownHandler> void async_shutdown(ShutdownHandler handler)
	{
		if (isSecure())
			mSocket->async_shutdown(handler);
		else
		{
			lowest_layer().shutdown(plain_socket::shutdown_both);
			mSocket->get_io_service().post(boost::bind(handler, error_code()));
		}
	}

	template <typename Seq, typename Handler> void async_read_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			mSocket->async_read_some(buffers, handler);
		else
			PlainSocket().async_read_some(buffers, handler);
	}

	template <typename Seq, typename Handler> void async_write_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			mSocket->async_write_some(buffers, handler);
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
			mSecure = false;
			cbFunc(ec);
		}
		else
		{ // ssl
			mSecure = true;
			mSocket->async_handshake(ssl_socket::server, cbFunc);
		}
	}
};

#endif

// vim:ts=4
