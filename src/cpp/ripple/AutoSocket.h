#ifndef __AUTOSOCKET_H_
#define __AUTOSOCKET_H_

#include <vector>
#include <string>

#include <boost/function.hpp>
#include <boost/bind.hpp>
#include <boost/smart_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/read_until.hpp>

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

	static bool rfc2818_verify(const std::string& domain, bool preverified, boost::asio::ssl::verify_context& ctx)
	{
		if (boost::asio::ssl::rfc2818_verification(domain)(preverified, ctx))
			return true;
		Log(lsWARNING, AutoSocketPartition) << "Outbound SSL connection to " <<
			domain << " fails certificate verification";
		return false;
	}

	boost::system::error_code verify(const std::string& strDomain)
	{
		boost::system::error_code ec;

	    mSocket->set_verify_mode(boost::asio::ssl::verify_peer);

		// XXX Verify semantics of RFC 2818 are what we want.
	    mSocket->set_verify_callback(boost::bind(&rfc2818_verify, strDomain, _1, _2), ec);

		return ec;
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
				boost::bind(&AutoSocket::handle_autodetect, this, cbFunc,
				basio::placeholders::error, basio::placeholders::bytes_transferred));
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

	template <typename Seq, typename Condition, typename Handler>
		void async_read_until(const Seq& buffers, Condition condition, Handler handler)
	{
		if (isSecure())
			basio::async_read_until(*mSocket, buffers, condition, handler);
		else
			basio::async_read_until(PlainSocket(), buffers, condition, handler);
	}

	template <typename Allocator, typename Handler>
		void async_read_until(basio::basic_streambuf<Allocator>& buffers, const std::string& delim, Handler handler)
	{
		if (isSecure())
			basio::async_read_until(*mSocket, buffers, delim, handler);
		else
			basio::async_read_until(PlainSocket(), buffers, delim, handler);
	}

	template <typename Allocator, typename MatchCondition, typename Handler>
		void async_read_until(basio::basic_streambuf<Allocator>& buffers, MatchCondition cond, Handler handler)
	{
		if (isSecure())
			basio::async_read_until(*mSocket, buffers, cond, handler);
		else
			basio::async_read_until(PlainSocket(), buffers, cond, handler);
	}

	template <typename Buf, typename Handler> void async_write(const Buf& buffers, Handler handler)
	{
		if (isSecure())
			boost::asio::async_write(*mSocket, buffers, handler);
		else
			boost::asio::async_write(PlainSocket(), buffers, handler);
	}

	template <typename Allocator, typename Handler>
		void async_write(boost::asio::basic_streambuf<Allocator>& buffers, Handler handler)
	{
		if (isSecure())
			boost::asio::async_write(*mSocket, buffers, handler);
		else
			boost::asio::async_write(PlainSocket(), buffers, handler);
	}

	template <typename Buf, typename Condition, typename Handler>
		void async_read(const Buf& buffers, Condition cond, Handler handler)
	{
		if (isSecure())
			boost::asio::async_read(*mSocket, buffers, cond, handler);
		else
			boost::asio::async_read(PlainSocket(), buffers, cond, handler);
	}

	template <typename Allocator, typename Condition, typename Handler>
		void async_read(basio::basic_streambuf<Allocator>& buffers, Condition cond, Handler handler)
	{
		if (isSecure())
			boost::asio::async_read(*mSocket, buffers, cond, handler);
		else
			boost::asio::async_read(PlainSocket(), buffers, cond, handler);
	}

	template <typename Buf, typename Handler> void async_read(const Buf& buffers, Handler handler)
	{
		if (isSecure())
			boost::asio::async_read(*mSocket, buffers, handler);
		else
			boost::asio::async_read(PlainSocket(), buffers, handler);
	}

	template <typename Seq, typename Handler> void async_write_some(const Seq& buffers, Handler handler)
	{
		if (isSecure())
			mSocket->async_write_some(buffers, handler);
		else
			PlainSocket().async_write_some(buffers, handler);
	}

protected:
	void handle_autodetect(callback cbFunc, const error_code& ec, size_t bytesTransferred)
	{
		if (ec)
		{
			Log(lsWARNING, AutoSocketPartition) << "Handle autodetect error: " << ec;
			cbFunc(ec);
		}
		else if ((mBuffer[0] < 127) && (mBuffer[0] > 31) &&
			((bytesTransferred < 2) || ((mBuffer[1] < 127) && (mBuffer[1] > 31))) &&
			((bytesTransferred < 3) || ((mBuffer[2] < 127) && (mBuffer[2] > 31))) &&
			((bytesTransferred < 4) || ((mBuffer[3] < 127) && (mBuffer[3] > 31))))
		{ // not ssl
			if (AutoSocketPartition.doLog(lsTRACE))
				Log(lsTRACE, AutoSocketPartition) << "non-SSL";
			mSecure = false;
			cbFunc(ec);
		}
		else
		{ // ssl
			if (AutoSocketPartition.doLog(lsTRACE))
				Log(lsTRACE, AutoSocketPartition) << "SSL";
			mSecure = true;
			mSocket->async_handshake(ssl_socket::server, cbFunc);
		}
	}
};

#endif

// vim:ts=4
