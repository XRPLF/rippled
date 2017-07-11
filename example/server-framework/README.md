<img width="880" height = "80" alt = "Beast"
    src="https://raw.githubusercontent.com/vinniefalco/Beast/master/doc/images/readme.png">

# HTTP and WebSocket built on Boost.Asio in C++11

## Server-Framework

This example is a complete, multi-threaded server built with Beast.
It contains the following components

* WebSocket ports (synchronous and asynchronous)
    - Echoes back any message received
    - Plain or SSL (if OpenSSL available)

* HTTP ports (synchronous and asynchronous)
    - Serves files from a configurable directory on GET request
    - Responds to HEAD requests with the appropriate result
    - Routes WebSocket Upgrade requests to a WebSocket port
    - Handles Expect: 100-continue
    - Supports pipelined requests
    - Plain or SSL (if OpenSSL available)

* Multi-Port: Plain, OpenSSL, HTTP, WebSocket **All on the same port!**

The server is designed to use modular components that users may simply copy
into their own project to get started quickly. Two concepts are introduced:

## PortHandler

The **PortHandler** concept defines an algorithm for handling incoming
connections received on a listening socket. The example comes with a
total of *nine* port handlers!

| Type  | Plain             | SSL                |           
| ----- | ----------------- | ------------------ |
| Sync  | `http_sync_port`  | `https_sync_port`  |
|       | `ws_sync_port`    | `wss_sync_port`    |
| Async | `http_async_port` | `https_async_port` |
|       | `wss_sync_port`   | `wss_async_port`   |
|       | `multi_port`      | `multi_port`       |


A port handler takes the stream object resulting form an incoming connection
request and constructs a handler-specific connection object which provides
the desired behavior.

The HTTP ports which come with the example have a system built in which allows
installation of framework and user-defined "HTTP services". These services
inform connections using the port on how to handle specific requests. This is
similar in concept to an "HTTP router" which is an element of most modern
servers.

These HTTP services are represented by the **Service** concept, and managed
in a container holding a type-list, called a `service_list`. Each HTTP port
allows the sevice list to be defined at compile-time and initialized at run
time. The framework provides these services:

* `file_service` Produces HTTP responses delivering files from a system path

* `ws_upgrade_service` Transports a connection requesting a WebSocket Upgrade
to a websocket port handler.

## Relationship

This diagram shows the relationship of the server object, to the nine
ports created in the example program, and the HTTP services contained by
the HTTP ports:

<img width="880" height = "344" alt = "ServerFramework"
    src="https://raw.githubusercontent.com/vinniefalco/Beast/master/doc/images/server.png">

## PortHandler Requirements
```C++
/** An synchronous WebSocket @b PortHandler which implements echo.

    This is a port handler which accepts WebSocket upgrade HTTP
    requests and implements the echo protocol. All received
    WebSocket messages will be echoed back to the remote host.
*/
struct PortHandler
{
    /** Accept a TCP/IP socket.

        This function is called when the server has accepted an
        incoming connection.

        @param sock The connected socket.

        @param ep The endpoint of the remote host.
    */
    void
    on_accept(
        socket_type&& sock,
        endpoint_type ep);
};
```

## Service Requirements

```C++
struct Service
{
    /** Initialize the service

        @param ec Set to the error, if any occurred
    */
    void
    init(error_code& ec);

    /** Maybe respond to an HTTP request

        Upon handling the response, the service may optionally
        take ownership of either the stream, the request, or both.

        @param stream The stream representing the connection

        @param ep The remote endpoint of the stream

        @param req The HTTP request

        @param send A function object which operates on a single
        argument of type beast::http::message. The function object
        has this equivalent signature:
        @code
        template<class Body, class Fields>
        void send(beast::http::response<Body, Fields>&& res);
        @endcode

        @return `true` if the service handled the response.
    */
    template<
        class Stream,
        class Body, class Fields,
        class Send>
    bool
    respond(
        Stream&& stream,
        endpoint_type const& ep,
        beast::http::request<Body, Fields>&& req,
        Send const& send) const
};
```

## Upgrade Service Requirements

To work with the `ws_upgrade_service`, a port or handler needs
this signature:
```C++

struct UpgradePort
{
    template<class Stream, class Body, class Fields>
    void
    on_upgrade(
        Stream&& stream,
        endpoint_type ep,
        beast::http::request<Body, Fields>&& req);

```
