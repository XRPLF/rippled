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

Beast.WSProto addresses the following goals:

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
http_request<http_string_body> m;
m.url = "/";
m.method = http::method_t::http_get;
m.version = 11; // HTTP/1.1
m.headers.insert("Host", "localhost");
m.headers.insert("User-Agent", "Beast.HTTP");
m.prepare();

```

After the HTTP request is created, it may be written to an established connection:
```C++
void do_request(ip::tcp::socket& sock,
    http_request<http_string_body>& m)
{
    http_write(sock, m);
}
```

## Types

### `basic_http_headers`
### `basic_http_message`
### `basic_http_request`
### `basic_http_response`
### `http_parser`

#### `Body` requirements

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

#### `Writer` requirements

* `X` denotes a type meeting the requirements of `Writer`
* `a` denotes a value of type `X`

expression                | return                | type assertion/note/pre/post-condition
:------------------------ |:----------------      |:--------------------------------------
`a.prepare(resume)`       | `boost::tribool`      | See `Writer` exemplar
`a.data()`                | `ConstBufferSequence` | See `Writer` exemplar

##### `Writer` exemplar

```C++
class Writer
{
public:
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
        
        @return `true` if there is data, `false` when done,
                boost::indeterminate to suspend.

        @note Undefined behavior if the callee takes ownership
              of resume but does not return boost::indeterminate.
    */
    boost::tribool
    prepare(resume_context&& resume);

    /** Return the next buffers to send.

        Preconditions:

            * Previous call to prepare() returned `true`
    */
    ConstBufferSequence
    data();
};
```

#### `HTTPParser` requirements:

In the table below, `X` denotes a class, `a` denotes a
value of type `X`, and `b` denotes a value convertible to
`boost::asio::const_buffer`.

 expression               | return        | type assertion/note/pre/post-condition
:------------------------ |:------------- |:--------------------------------------
`a.complete()`            | `bool`        | Returns `true` if the parsing is complete

