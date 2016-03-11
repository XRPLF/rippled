# Beast.HTTP

--------------------------------------------------------------------------------

Beast.HTTP offers programmers simple and performant models of HTTP
messages and their associated operations including synchronous and
asynchronous reading and writing using Boost.Asio.

## Introduction

The HTTP protocol is pervasive in network applications. As C++ is a
logical choice for high performance network servers, there is great
utility in solid building blocks for manipulating, sending, and
receiving HTTP messages compliant with RFC2616 and its supplements
that follow. Unfortunately popular libraries such as Boost or the
C++ standard library do not provide support for this popular protocol.

Beast.HTTP is built on Boost.Asio and uses HTTP parser from NodeJS,
which is extensively field tested and exceptionally robust.
A proposal to add networking functionality to the C++ standard library,
based on Boost.Asio, is under consideration by the standards committee.
Since the final approved networking interface for the C++ standard library
will likely closely resemble the current interface of Boost.Asio, it is
logical for Beast.HTTP to use Boost.Asio as its network transport.

Beast.HTTP addresses the following goals:

* **Ease of Use** HTTP messages are modeled using simple, readily
accessible objects.

* **Flexibility** The modeling of the HTTP message should allow for
multiple implementation strategies for representing the content-body.

* **Performance** The implementation should run sufficiently fast as to
make it a competitive choice for building a high performance network
server.

* **Scalability.** The library should facilitate the development of
network applications that scale to thousands of concurrent connections.

## Example

All examples and identifiers mentioned in this document are written as
if the following declarations are in effect:
```C++
#include <beast/http.h>
using namespace beast;
using namespace boost::asio;
```

Create a HTTP request:
```C++
request<string_body> req(method_t::http_get, "/", 11);
req.headers.insert("Host", "127.0.0.1:80");
req.headers.insert("User-Agent", "Beast.HTTP");

```

A `message` (`request` or `response`) is like an editable document.
Callers make changes until the desired final object state is reached.
To send a message it must first be prepared, which transforms it into
a `prepared_message` ready for sending. The Body associated with the
message will perform any steps necessary for preparation. For example,
a string body will set the Content-Length and Content-Type appropriately.
```C++
void send_request(ip::tcp::socket& sock,
    request<string_body>&& req)
{
    // Send the request on the socket
    write(sock, prepare(req, keep_alive{true}));
}
```

Messages can be read from the network and parsed into a `parsed_message`
object, which extends the `message` by adding parse-specific metadata such
as the keep-alive which is context sensitive (depending on the HTTP version
for example). When preparing a response for sending, `prepare` must be
called with an additional parameter, the corresponding parsed request.
The implementation inspects the contents of the request to set dependent
fields of the response. This example reads a message, builds a response,
and sends it.
```C++
void handle_connection(ip::tcp::socket& sock)
{
    parsed_request<string_body> req;
    read(sock, req);
    response<string_body> resp;
    ...
    write(sock, prepare(resp, req));
}
```

## Modeling the HTTP message



## Types

Definitions for Body, Reader, and Writer member functions should typically
be marked inline so they become part of the code that calls them.

### `Body` requirements

`X` denotes a class meeting the requirements of `HTTPBody`
`a` denotes a value of type `X`
`sb` denotes an object meeting the requirements of `Streambuf`.

 expression               | return        | type assertion/note/pre/post-condition
:------------------------ |:------------- |:--------------------------------------
`X`                       |               | A type meeting the requirements of `HTTPBody`
`a`                       | `X`           | `a` is a value of type `X`
`sb`                      |               | `sb` is any `Streambuf`
`m`                       |               | `m` is any `HTTPMessage`
`a.prepare(m)`            |               | Prepare `a` for serialization (called once)
`a.write(sb)`             |               | Serializes `a` to a `Streambuf`

### `Reader` requirements

* `X` denotes a type meeting the requirements of `Writer`
* `a` denotes a value of type `X`

expression                | return                | type assertion/note/pre/post-condition
:------------------------ |:----------------      |:--------------------------------------
`a.prepare(resume)`       | `boost::tribool`      | See `Writer` exemplar
`a.data()`                | `ConstBufferSequence` | See `Writer` exemplar

### `Writer` requirements

* `X` denotes a type meeting the requirements of `Writer`
* `a` denotes a value of type `X`

expression                | return                | type assertion/note/pre/post-condition
:------------------------ |:----------------      |:--------------------------------------
`a.prepare(resume)`       | `boost::tribool`      | See `Writer` exemplar
`a.data()`                | `ConstBufferSequence` | See `Writer` exemplar

### Single-pass `Writer` exemplar
```C++
struct Writer
{
    /** Construct the writer.

        The msg object is guaranteed to exist for the
        lifetime of the writer.

        Exceptions:

            No-throw guarantee.
    */
    template<bool isRequest, class Body, class Allocator>
    writer(message<isRequest, Body, Allocator> const& msg) nothrow;

    /** Return a buffer sequence representing the entire message.

        Exceptions:

            No-throw guarantee.

        @note The implementation of Writer is responsible for
        serializing the headers, using the provided functions.
    */
    ConstBufferSequence
    data() nothrow;
}
```

### Multi-pass `Writer` exemplar
```C++
struct Writer
{
    /** Construct the writer.

        The msg object is guaranteed to exist for the
        lifetime of the writer.

        @param msg The message to be written.

        Exceptions:

            No-throw guarantee.
    */
    template<bool isRequest, class Body, class Allocator>
    writer(message<isRequest, Body, Allocator> const& msg) nothrow;

    /** Initialize the writer.

        Called once before any calls to prepare. The writer
        can perform initialization which may fail.

        Exceptions:

            No-throw guarantee.

        @param ec Contains the error code if any errors occur.
    */
    void
    init(error_code& ec) noexcept;

    /** Prepare the next buffer for sending.

        Postconditions:

        If return value is `true`:

            * Callee does not take ownership of resume.
            * Caller calls data() to retrieve the next
              buffer sequence to send.

        If return value is `false`:

            * Callee does not take ownership of resume.
            * The write operation is completed.

        If return value is boost::indeterminate:

            * Callee takes ownership of resume.
            * Caller suspends the write operation
              until resume is invoked.

        When the caller takes ownership of resume, the
        asynchronous operation will not complete until the
        caller destroys the object.

        Exceptions:

            No-throw guarantee.

        @param resume A functor to call to resume the write operation
        after the writer has returned boost::indeterminate.

        @param ec Set to indicate an error. This will cause an
        asynchronous write operation to complete with the error.

        @return `true` if there is data, `false` when done,
                boost::indeterminate to suspend.

        @note Undefined behavior if the callee takes ownership
              of resume but does not return boost::indeterminate.
    */
    boost::tribool
    prepare(resume_context&& resume, error_code& ec) noexcept;

    /** Return the next buffers to send.

        Preconditions:

            * Previous call to prepare() returned `true`

        Exceptions:

            No-throw guarantee.

        @return A ConstBufferSequence representing the buffers
        to be sent next.

        @note The implementation of Writer is responsible for
        serializing the headers, using the provided functions.
    */
    ConstBufferSequence
    data() noexcept;
};
```

