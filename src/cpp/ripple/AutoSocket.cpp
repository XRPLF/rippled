
#include "AutoSocket.h"

#include <boost/bind.hpp>

void AutoSocket::handle_autodetect(const error_code& ec)
{
	if (ec)
	{
		if (mCallback)
			mCallback(ec);
		return;
	}

	if ((mBuffer[0] < 127) && (mBuffer[0] > 31) &&
		(mBuffer[1] < 127) && (mBuffer[1] > 31) &&
		(mBuffer[2] < 127) && (mBuffer[2] > 31) &&
		(mBuffer[3] < 127) && (mBuffer[3] > 31))
	{ // non-SSL
		mSecure = false;
		if (mCallback)
			mCallback(ec);
	}
	else
	{ // ssl
		mSecure = true;
		SSLSocket().async_handshake(ssl_socket::server, mCallback);
	}
}

// vim:ts=4
