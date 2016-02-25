# Beast.WSProto

--------------------------------------------------------------------------------

Beast.WSProto provides developers with a robust WebSocket implementation
built on Boost.Asio with a consistent asynchronous model using a modern
C++ approach.

## Introduction

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
logical for Beast.WSProto to use Boost.Asio as its network transport.

Beast.WSProto addresses the following goals:

* **Ease of Use.** WSProto offers only one socket object, whose interface
resembles that of Boost.Asio socket as closely as possible. Users familiar
with Boost.Asio will be immediately comfortable using a `wsproto::socket`.

* **Flexibility.** Library interfaces should provide callers with maximum
flexibility in implementation; Important decisions such as how to manage
buffers or be notified of completed asynchronous operations should be made
by callers not the library.

* **Performance.** The implementation should achieve the highest level
of performance possible, with no penalty for using abstractions.

* **Scalability.** The library should facilitate the development of
network applications that scale to thousands of concurrent connections.

* **Efficiency.** The library should support techniques such as
scatter-gather I/O, and allow programs to minimise data copying.

* **Basis for further abstraction.** The library should permit the
development of other libraries that provide higher levels of abstraction.

Beast.WSProto takes advantage of Boost.Asio's universal Asynchronous
model, handler allocation, and handler invocation hooks. Calls to wsproto
asynchronous initiation functions allow callers the choice of using a
completion handler, stackful or stackless coroutines, futures, or user
defined customizations (for example, Boost.Fiber). The implementation
uses handler invocation hooks (`asio_handler_invoke`), providing
execution guarantees on composed operations in a manner identical to
Boost.Asio. The implementation also uses handler allocation hooks
(`asio_handler_allocate`) when allocating memory internally for composed
operations.

There is no need for inheritance or virtual members in `wsproto::socket`.
All operations are templated and transparent to the compiler, allowing for
maximum inlining and optimization.

## Usage

All examples and identifiers mentioned in this document are written as
if the following declarations are in effect:
```C++
#include <beast/wsproto.h>
using namespace beast;
using namespace boost::asio;
```

### Creating a Socket

To participate in a WebSocket connection, callers create an instance
of `wsproto::socket` templated on the `Stream` argument, which must meet
the requirements of `AsyncReadStream`, `AsyncWriteStream`, `SyncReadStream`,
and `SyncWriteStream`. Examples of types that meet these requirements are
`ip::tcp::socket` and `ssl::stream<...>`:
```c++  
io_service ios;  
wsproto::socket<ip::tcp::socket> ws1(ios);      // owns the socket  
    
ssl::context ctx(ssl::context::sslv23);  
wsproto::socket<ssl::stream<  
    ip::tcp::socket>> wss(ios, ctx);            // owns the socket  

ip::tcp::socket sock(ios);  
wsproto::socket<ip::tcp::socket&> ws2(sock);    // does not own the socket  
```

### Connection Establishment

Callers are responsible for performing tasks such as connection establishment
before attempting websocket activities.
```c++
io_service ios;
wsproto::socket<ip::tcp::socket> ws(ios);
ws.next_layer().connect(ip::tcp::endpoint(
    ip::tcp::address::from_string("127.0.0.1"), 80));
```

### WebSocket Handshake

After the connection is established, the socket may be used to initiate
or accept a WebSocket Update request.

```c++
// send a WebSocket Upgrade request.
ws.handshake();
```

### Sending and Receiving Messages

After the WebSocket handshake is accomplished, callers may send and receive
messages using the message oriented interface:
```c++
void echo(wsproto::socket<ip::tcp::socket>& ws)
{
    streambuf sb;
    wsproto::opcode op;
    wsproto::read(ws, op, sb);
    wsproto::write(ws, op, sb.data());
    sb.consume(sb.size());
}
```

Alternatively, callers may process incoming message data
incrementally:
```c++
void echo(wsproto::socket<ip::tcp::socket>& ws)
{
    streambuf sb;
    wsproto::msg_info mi{};
    for(;;)
    {
        ws.read_some(mi, sb);
        if(mi.fin)
            break;
    }
    wsproto::write(ws, op, sb.data());
}
```

### Asynchronous Completions, Coroutines, and Futures

Asynchronous versions are available for all functions:
```c++
wsproto::async_read(ws, sb, std::bind(
    &on_read, beast::asio::placeholders::error));
```

Calls to WSProto asynchronous initiation functions support
asio-style completion handlers, and other completion tokens
such as support for coroutines or futures:
```c++
void echo(wsproto::socket<ip::tcp::socket>& ws,
    boost::asio::yield_context yield)
{
    wsproto::async_read(ws, sb, yield);
    std::future<wsproto::error_code> fut =
        wsproto::async_write(ws, sb.data(), boost::use_future);
    ...
}
```

## Implementation

### Buffers

Because calls to read WebSocket data may return a variable amount of bytes,
the interface to calls that read data require an object that meets the
requirements of `Streambuf`. This concept is modeled on
`boost::asio::basic_streambuf`, which meets the requirements of `Streambuf`
defined below.

The `Streambuf` concept is intended to permit the following implementation
strategies:

* A single contiguous character array, which is reallocated as necessary to
  accommodate changes in the size of the byte sequence. This is the
  implementation approach currently used in `boost::asio::basic_streambuf`.
* A sequence of one or more byte arrays, where each array is of the same
  size. Additional byte array objects are appended to the sequence to
  accommodate changes in the size of the byte sequence.
* A sequence of one or more byte arrays of varying sizes. Additional byte
  array objects are appended to the sequence to accommodate changes in the
  size of the byte sequence. This is the implementation approach currently
  used in `beast::basic_streambuf`.

#### `Streambuf` requirements:

In the table below, `X` denotes a class, `a` denotes a value
of type `X`, `n` denotes a value convertible to `std::size_t`,
and `U` and `T` denote unspecified types.

expression                | return        | type assertion/note/pre/post-condition
------------------------- | ------------- | --------------------------------------
`X::const_buffers_type`   | `T`           | `T` meets the requirements for `ConstBufferSequence`.
`X::mutable_buffers_type` | `U`           | `U` meets the requirements for `MutableBufferSequence`.
`a.commit(n)`             |               | Moves bytes from the output sequence to the input sequence.
`a.consume(n)`            |               | Removes bytes from the input sequence.
`a.data()`                | `T`           | Returns a list of buffers that represents the input sequence.
`a.prepare(n)`            | `U`           | Returns a list of buffers that represents the output sequence, with the given size.
`a.size()`                | `std::size_t` | Returns the size of the input sequence.
`a.max_size()`            | `std::size_t` | Returns the maximum size of the `Streambuf`.

### Thread Safety

Like a regular asio socket, a `wsproto::socket` is not thread safe. Callers are
responsible for synchronizing operations on the socket using an implicit or
explicit strand, as per the Asio documentation. A `wsproto::socket` supports
one active read and one active write at the same time (caller initiated close,
ping, and pong operations count as a write).

### Buffering

The implementation does not perform queueing or buffering of messages. If desired,
these features should be implemented by callers. The impact of this design is
that the caller is in full control of the allocation strategy used to store
data and the back-pressure applied on the read and write side of the underlying
TCP/IP connection.

### The `io_service`

The creation and operation of the `boost::asio::io_service` associated with the
Stream object underlying the `wsproto::socket` is completely left up to the
user of the library, permitting any implementation strategy including one that
does not require threads for environments where threads are unavailable.
Beast.WSProto itself does not use or require threads.
