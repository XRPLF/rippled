# Beast.WSProto

--------------------------------------------------------------------------------

Beast.WSProto provides developers with a robust WebSocket implementation
built on Boost.Asio with a consistent asynchronous model using a modern
C++ approach.

## Rationale

Today's web applications increasingly rely on alternatives to standard HTTP
to achieve performance and/or responsiveness. While WebSocket implementations
are widely available in common web development languages such as Javascript,
good implementations in C++ are scarce. A survey of existing C++ WebSocket
solutions reveals interfaces which have performance limitations, place
unecessary restrictions on callers, exhibit excess complexity, and fail to
take advantage of C++ features or the underlying network transport.

Beast.WSProto is built on Boost.Asio, a robust cross platform networking
framework that is part of Boost and also offered as a standalone library.
A proposal to add networking functionality to the C++ standard library,
based on Boost.Asio, is under consideration by the standards committee.
Since the final approved networking interface for the C++ standard library
will likely closely resemble the current interface of Boost.Asio, it is
logical for Beast.WSProto to choose Boost.Asio as its network transport.

Beast.WSProto addresses the following goals:

* **Performance.** The implementation should achieve the highest level
of performance possible, with no penalty for using abstractions.

* **Scalability.** The library should facilitate the development of
network applications that scale to thousands of concurrent connections.

* **Efficiency.** The library should support techniques such as
scatter-gather I/O, and allow programs to minimise data copying.

* **Flexibility.** Library interfaces should provide callers with maximum
flexibility in implementation; Important decisions such as how to manage
buffers or be notified of completed asynchronous operations should be made
by callers not the library.

* **Basis for further abstraction.** The library should permit the
development of other libraries that provide higher levels of abstraction.

## Introduction

All examples and identifiers mentioned in this document are written as
if the following declarations are in effect:
```C++
using namespace beast;
using namespace boost::asio;
```

To participate in a WebSocket connection, callers create an instance
of `wsproto::socket` templated on the `Stream` argument, which must meet
the requirements of `AsyncReadStream`, `AsyncWriteStream`, `SyncReadStream`,
and `SyncWriteStream`. Examples of types that meet these requirements are
`ip::tcp::socket` and `ssl::stream<...>`:
```c++
io_service ios;
wsproto::socket<ip::tcp::socket> ws1(ios);      // owns the socket

ip::tcp::socket sock(ios);
wsproto::socket<ip::tcp::socket&> ws2(sock);    // does not own the socket

ssl::context ctx(ssl::context::sslv23);
wsproto::socket<ssl::stream<
    ip::tcp::socket>> wss(ios, ctx);            // owns the socket
```

Callers are responsible for performing tasks such as connection establishment
before attempting websocket activities.
```c++
io_service ios;
wsproto::socket<ip::tcp::socket> ws(ios);
ws.next_layer().connect(ip::tcp::endpoint(
    ip::tcp::address::from_string("0.0.0.0"), 80));

// send a WebSocket Upgrade request.
ws.handshake();
```

After a connection is established callers may send and receive
messages synchronously:
```c++
void echo(wsproto::socket<ip::tcp::socket>& ws)
{
    streambuf sb;
    wsproto::read(ws, sb);
    wsproto::write(ws, sb.data());
}
```

## `Streambuf` requirements

In the table below, `X` denotes a class, `a` denotes a value
of type `X`, `n` denotes a value convertible to `std::size_t`,
and `U` and `T` denote unspecified types.

expression                  | return        | type assertion/note/pre/post-condition
--------------------------- | ------------- | --------------------------------------
`X::const_buffers_type`     | `T`           | `T` meets the requirements for `ConstBufferSequence`.
`X::mutable_buffers_type`   | `U`           | `U` meets the requirements for `MutableBufferSequence`.
`a.commit(n)`               |               | Moves bytes from the output sequence to the input sequence.
`a.consume(n)`              |               | Removes bytes from the input sequence.
`a.data()`                  | `T`           | Returns a list of buffers that represents the input sequence.
`a.prepare(n)`              | `U`           | Returns a list of buffers that represents the output sequence, with the given size.
`a.size()`                  | `std::size_t` | Returns the size of the input sequence.
`a.max_size()`              | `std::size_t` | Returns the maximum size of the `Streambuf`.
