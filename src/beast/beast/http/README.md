# Beast.HTTP

--------------------------------------------------------------------------------

Beast.HTTP offers programmers simple and performant models of HTTP messages and
their associated operations including synchronous and asynchronous reading and
writing using Boost.Asio.

## Introduction

The HTTP protocol is pervasive in network applications. As C++ is a logical
choice for high performance network servers, there is great utility in solid
building blocks for manipulating, sending, and receiving HTTP messages compliant
with RFC2616 and its supplements that follow. Unfortunately popular libraries
such as Boost or the C++ standard library do not provide support for this
popular protocol.

Beast.HTTP is built on Boost.Asio and uses HTTP parser from NodeJS, which is
extensively field tested and exceptionally robust. A proposal to add networking
functionality to the C++ standard library, based on Boost.Asio, is under
consideration by the standards committee. Since the final approved networking
interface for the C++ standard library will likely closely resemble the current
interface of Boost.Asio, it is logical for Beast.HTTP to use Boost.Asio as its
network transport.

Beast.HTTP addresses the following goals:

* **Ease of Use:** HTTP messages are modeled using simple, readily
accessible objects.

* **Flexibility:** The modeling of the HTTP message should allow for
multiple implementation strategies for representing the content-body.

* **Performance:** The implementation should run sufficiently fast as
to make it a competitive choice for building a high performance network
server.

* **Scalability:** The library should facilitate the development of
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

To send a message it must first be prepared through a call to `prepare`. This
customization point transforms the `message` into a `prepared_message`,
filling in some standard HTTP behavior and allowing the Body associated with
the message to perform preparatory steps. For example, a string body may set
the Content-Length and Content-Type appropriately.
```C++
void send_request(ip::tcp::socket& sock,
    request<string_body>&& req)
{
    // Send the request on the socket
    write(sock, prepare(req, connection(keep_alive));
}
```

Messages can be read from the network and parsed into a `parsed_message` object,
which extends the `message` by adding parse-specific metadata such as the
keep-alive which is context sensitive (depending on the HTTP version for
example). When preparing a response for sending, `prepare` must be called with
an additional parameter, the corresponding parsed request. The implementation
inspects the contents of the request to set dependent fields of the response.
This example reads a message, builds a response, and sends it.
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

All HTTP messages are modeled using this base class template:
```C++
template<bool isRequest, class Body, class Allocator>
class message
{
    ...
    typename Body::value_type body;
}

template<class Body, class Allocator>
using request = message<true, Body, Allocator>;

template<class Body, class Allocator>
using response = message<false, Body, Allocator>;
```

The template argument `isRequest` is `true` for HTTP requests and `false`
for HTTP responses, allowing functions to be overloaded or constrained
based on the type of message they want to be passed.

The `Body` template argument controls the method used to store information
necessary for receiving or sending the body, as well as providing customizations
for the actual writing or parsing process. The customizations are used by the
implementation to perform the `read`, `write`, `async_read`, and `async_write`
operations on messages.

Beast.HTTP offers `empty_body`, `string_body`, and `streambuf_body` as common
choices for the `Body` template argument. User-defined objects that meet the `Body`
requirements may be implemented for custom implementation strategies.

*Note:* Definitions for member functions associated with a `Body` and the types
it defines should typically be declared inline so they become part of the calling
code.

## Concepts

### `Body`

Requirements:

`req` denotes any instance of `prepared_request`.<br>
`resp` denotes any instance of `prepared_response`.<br>
`preq` denotes any instance of `parsed_request`.<br>

 expression                 | return | type assertion/note/pre/post-condition
:-------------------------- |:------ |:--------------------------------------
`Body::value_type`          |        | The type of the `message::body` member.
`Body::value_type{}`        |        | `DefaultConstructible`
`Body::reader`              |        | A type meeting the requirements of `Reader`
`Body::writer`              |        | A type meeting the requirements of `SinglePassWriter` or `MultiPassWriter`
`Body::prepare(req)`        |        | Prepare `req` for serialization
`Body::prepare(resp, preq)` |        | Prepare `resp` for serialization

An instance of `message` will inherit the movability and copyability attribuytes
of the associated `Body::value_type`. That is to say, if the `Body::value_type`
is movable, then `message` objects using that body will also be movable.

### `Reader`

The implementation for the HTTP parser will construct the body's corresponding
`reader` object during the parse process. This customization point allows the
body to determine the strategy for storing incoming body data.

Requirements:

`X`  denotes a type meeting the requirements of `Reader`.<br>
`a`  denotes a value of type `X`.<br>
`m`  denotes a value of type `message const&` where
       `std::is_same<decltype(m.body), Body::value_type>:value == true`.<br>
`p`  is any pointer.<br>
`n`  is a value convertible to `std::size_t`.<br>
`ec` is a value of type `error_code&`.<br>

 expression         | return | type assertion/note/pre/post-condition
:------------------ |:------ |:--------------------------------------
`X a(m);`           |        | `a` is no-throw constructible from `m`.
`a.write(p, n, ec)` |        | No-throw guarantee

### `Writer`

Calls to readers and writers used in an asynchronous operations will be made in
the same fashion as that used to invoke the final completion handler.

### `SinglePassWriter`

A single-pass writer serializes a HTTP content body that can be fully represented
in memory as a single `ConstBufferSequence`. The implementation of `write` will
construct the object of this type corresponding to the `Body` of the message being
sent, and send the buffer sequence it produces on the socket.

Requirements:

`X` denotes a type meeting the requirements of `SinglePassWriter`.<br>
`a` denotes a value of type `X`.<br>
`m` denotes a value of type `message const&` where
      `std::is_same<decltype(m.body), Body::value_type>:value == true`.<br>

expression                | return                | type assertion/note/pre/post-condition
:------------------------ |:----------------      |:--------------------------------------
`X a(m);`                 |                       | `a` is no-throw constructible from `m`.
`X::is_single_pass`       | `bool`                | `true`
`a.prepare(resume)`       | `boost::tribool`      | See `Writer` exemplar
`a.data()`                | `ConstBufferSequence` | See `Writer` exemplar

Exemplar:
```C++
struct SinglePassWriter
{
    /** Construct the writer.
        The msg object is guaranteed to exist for the lifetime of the writer.
        @param msg The message to be written.
        Exceptions:
            No-throw guarantee.
    */
    template<bool isRequest, class Body, class Allocator>
    SinglePassWriter(
        message<isRequest, Body, Allocator> const& msg) noexcept;

    /** Return the next buffers to send.
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

### `MultiPassWriter`

A multi-pass writer serializes HTTP content bodies that may not be fully resident
in memory, or may be incrementally rendered. For example, a response containing the
contents of a file on disk. The `write` implementation makes zero or more calls to
the multi-pass writer to produce each set of buffers. The interface of
`MultiPassWriter` is intended to allow the following implementation strategies:

* Return a body that does not entirely fit in memory.
* Return a body produced incrementally from coroutine output.
* Return a series of buffers when the content size is not known ahead of time.
* Return body data on demand from foreign threads using suspend and resume semantics.
* Return a body computed algorithmically.

Requirements:

`X`  denotes a type meeting the requirements of `MultiPassWriter`.<br>
`a`  denotes a value of type `X`.<br>
`m`  denotes a value of type `message const&` where
        `std::is_same<decltype(m.body), Body::value_type>:value == true`.<br>
`r`  is an object of type resume_context.
`ec` is a value of type `error_code&`.<br>

expression                | return                | type assertion/note/pre/post-condition
:------------------------ |:----------------      |:--------------------------------------
`X a(m);`                 |                       | `a` is no-throw constructible from `m`.
`X::is_single_pass`       | `bool`                | `false`
`a.init(ec)`              |                       | Called immediately after construction. No-throw guarantee.
`a.prepare(r, ec)`        | `boost::tribool`      | No-throw guarantee. See `Writer` exemplar
`a.data()`                | `ConstBufferSequence` | No-throw guarantee. See `Writer` exemplar

Exemplar:
```C++
struct MultiPassWriter
{
    /** Construct the writer.
        The msg object is guaranteed to exist for the lifetime of the writer.
        @param msg The message to be written.
        Exceptions:
            No-throw guarantee.
    */
    template<bool isRequest, class Body, class Allocator>
    MultiPassWriter(
        message<isRequest, Body, Allocator> const& msg) noexcept;

    /** Initialize the writer.
        Called once before any calls to prepare. The writer can perform
        initialization which may fail.
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

## `string_body` Example:
```C++
struct string_body
{
    using value_type = std::string;

    class reader
    {
        value_type& s_;

    public:
        template<bool isReq, class Allocator>
        explicit
        reader(message<isReq,
                string_body, Allocator>& m) noexcept
            : s_(m.body)
        {
        }

        void
        write(void const* data,
            std::size_t size, error_code&) noexcept
        {
            auto const n = s_.size();
            s_.resize(n + size);
            std::memcpy(&s_[n], data, size);
        }
    };

    class writer
    {
        streambuf sb;
        boost::asio::const_buffers_1 cb;

    public:
        static bool constexpr is_single_pass = true;

        template<bool isReq, class Allocator>
        explicit
        writer(message<isReq, string_body, Allocator> const& m) noexcept
            : cb(boost::asio::buffer(m.body))
        {
            m.write_headers(sb);
        }

        void
        init(error_code& ec) noexcept
        {
        }

        auto
        data() const noexcept
        {
            return append_buffers(sb.data(), cb);
        }
    };

    template<bool isRequest, class Allocator>
    static
    void
    prepare(prepared_message<isRequest, string_body, Allocator>& msg)
    {
        msg.headers.replace("Content-Length", msg.body.size());
        if(msg.body.size() > 0)
            msg.headers.replace("Content-Type", "text/html");
    }

    template<class Allocator,
        class OtherBody, class OtherAllocator>
    static
    void
    prepare(prepared_message<false, string_body, Allocator>& msg,
        parsed_message<true, OtherBody, OtherAllocator> const&)
    {
        prepare(msg);
    }
};
```
