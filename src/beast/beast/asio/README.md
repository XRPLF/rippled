# beast::asio

Wrappers and utilities to make working with boost::asio easier.

## Rules for asynchronous objects

If an object calls asynchronous initiating functions it must either:

	1. Manage its lifetime by being reference counted

	or

	2. Wait for all pending completion handlers to be called before
	   allowing itself to be destroyed.
