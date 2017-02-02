<img width="880" height = "80" alt = "Beast"
    src="https://raw.githubusercontent.com/vinniefalco/Beast/master/doc/images/readme.png">

[![Join the chat at https://gitter.im/vinniefalco/Beast](https://badges.gitter.im/vinniefalco/Beast.svg)](https://gitter.im/vinniefalco/Beast?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge) [![Build Status]
(https://travis-ci.org/vinniefalco/Beast.svg?branch=master)](https://travis-ci.org/vinniefalco/Beast) [![codecov]
(https://codecov.io/gh/vinniefalco/Beast/branch/master/graph/badge.svg)](https://codecov.io/gh/vinniefalco/Beast) [![coveralls]
(https://coveralls.io/repos/github/vinniefalco/Beast/badge.svg?branch=master)](https://coveralls.io/github/vinniefalco/Beast?branch=master) [![Documentation]
(https://img.shields.io/badge/documentation-master-brightgreen.svg)](http://vinniefalco.github.io/beast/) [![License]
(https://img.shields.io/badge/license-boost-brightgreen.svg)](LICENSE_1_0.txt)

# HTTP and WebSocket built on Boost.Asio in C++11

---

## Appearances

| <a href="http://cppcast.com/2017/01/vinnie-falco/">CppCast 2017</a> | <a href="https://raw.githubusercontent.com/vinniefalco/Beast/master/doc/images/CppCon2016.pdf">CppCon 2016</a> |
| ------------ | ----------- |
| <a href="http://cppcast.com/2017/01/vinnie-falco/"><img width="180" height="180" alt="Vinnie Falco" src="https://avatars1.githubusercontent.com/u/1503976?v=3&u=76c56d989ef4c09625256662eca2775df78a16ad&s=180"></a> | <a href="https://www.youtube.com/watch?v=uJZgRcvPFwI"><img width="320" height = "180" alt="Beast" src="https://raw.githubusercontent.com/vinniefalco/Beast/master/doc/images/CppCon2016.png"></a> |

---

## Contents

- [Introduction](#introduction)
- [Description](#description)
- [Requirements](#requirements)
- [Building](#building)
- [Usage](#usage)
- [Licence](#licence)
- [Contact](#contact)

## Introduction

Beast is a header-only, cross-platform C++ library built on Boost.Asio and
Boost, containing two modules implementing widely used network protocols.
Beast.HTTP offers a universal model for describing, sending, and receiving
HTTP messages while Beast.WebSocket provides a complete implementation of
the WebSocket protocol. Their design achieves these goals:

* **Symmetry.** Interfaces are role-agnostic; the same interfaces can be
used to build clients, servers, or both.

* **Ease of Use.** HTTP messages are modeled using simple, readily
accessible objects. Functions and classes used to send and receive HTTP
or WebSocket messages are designed to resemble Boost.Asio as closely as
possible. Users familiar with Boost.Asio will be immediately comfortable
using this library.

* **Flexibility.** Interfaces do not mandate specific implementation
strategies; important decisions such as buffer or thread management are
left to users of the library.

* **Performance.** The implementation performs competitively, making it a
realistic choice for building high performance network servers.

* **Scalability.** Development of network applications that scale to thousands
of concurrent connections is possible with the implementation.

* **Basis for further abstraction.** The interfaces facilitate the
development of other libraries that provide higher levels of abstraction.

Beast is used in [rippled](https://github.com/ripple/rippled), an
open source server application that implements a decentralized
cryptocurrency system.

## Description

This software is currently in beta: interfaces may change.
For recent changes see the [CHANGELOG](CHANGELOG.md).
The library has been submitted to the
[Boost Library Incubator](http://rrsd.com/blincubator.com/bi_library/beast-2/?gform_post_id=1579)

* [Project Site](http://vinniefalco.github.io/)
* [Repository](https://github.com/vinniefalco/Beast)
* [Project Documentation](http://vinniefalco.github.io/beast/)
* [Autobahn.testsuite results](http://vinniefalco.github.io/autobahn/index.html)

## Requirements

* Boost 1.58 or higher
* C++11 or greater
* OpenSSL (optional)

## Building

Beast is header-only so there are no libraries to build or link with.
To use Beast in your project, simply copy the Beast sources to your
project's source tree (alternatively, bring Beast into your Git repository
using the `git subtree` or `git submodule` commands). Then, edit your
 build scripts to add the `include/` directory to the list of paths checked
 by the C++ compiler when searching for includes. Beast `#include` lines
 will look like this:
```C++
#include <beast/http.hpp>
#include <beast/websocket.hpp>
```

To link your program successfully, you'll need to add the Boost.System
library to link with. If you use coroutines you'll also need the
Boost.Coroutine library. Please visit the Boost documentation for
instructions on how to do this for your particular build system.

For the examples and tests, Beast provides build scripts for Boost.Build (bjam)
and CMake. Developers using Microsoft Visual Studio can generate Visual Studio
project files by executing these commands from the root of the repository:


```
cd bin
cmake ..                                    # for 32-bit Windows build

cd ../bin64
cmake ..                                    # for Linux/Mac builds, OR
cmake -G"Visual Studio 14 2015 Win64" ..    # for 64-bit Windows builds
```

To build with Boost.Build, it is necessary to have the bjam executable
in your path. And bjam needs to know how to find the Boost sources. The
easiest way to do this is make sure that the version of bjam in your path
is the one at the root of the Boost source tree, which is built when
running `bootstrap.sh` (or `bootstrap.bat` on Windows).

Once bjam is in your path, simply run bjam in the root of the Beast
repository to automatically build the required Boost libraries if they
are not already built, build the examples, then build and run the unit
tests.

The files in the repository are laid out thusly:

```
./
    bin/            Holds executables and project files
    bin64/          Holds 64-bit Windows executables and project files
    doc/            Source code and scripts for the documentation
    include/        Add this to your compiler includes
        beast/
    extras/         Additional APIs, may change
    examples/       Self contained example programs
    test/           Unit tests and benchmarks
```


## Usage

These examples are complete, self-contained programs that you can build
and run yourself (they are in the `examples` directory).

Example WebSocket program:
```C++
#include <beast/core/to_string.hpp>
#include <beast/websocket.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <string>

int main()
{
    // Normal boost::asio setup
    std::string const host = "echo.websocket.org";
    boost::asio::io_service ios;
    boost::asio::ip::tcp::resolver r{ios};
    boost::asio::ip::tcp::socket sock{ios};
    boost::asio::connect(sock,
        r.resolve(boost::asio::ip::tcp::resolver::query{host, "80"}));

    // WebSocket connect and send message using beast
    beast::websocket::stream<boost::asio::ip::tcp::socket&> ws{sock};
    ws.handshake(host, "/");
    ws.write(boost::asio::buffer(std::string("Hello, world!")));

    // Receive WebSocket message, print and close using beast
    beast::streambuf sb;
    beast::websocket::opcode op;
    ws.read(op, sb);
    ws.close(beast::websocket::close_code::normal);
    std::cout << beast::to_string(sb.data()) << "\n";
}
```

Example HTTP program:
```C++
#include <beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <iostream>
#include <string>

int main()
{
    // Normal boost::asio setup
    std::string const host = "boost.org";
    boost::asio::io_service ios;
    boost::asio::ip::tcp::resolver r{ios};
    boost::asio::ip::tcp::socket sock{ios};
    boost::asio::connect(sock,
        r.resolve(boost::asio::ip::tcp::resolver::query{host, "http"}));

    // Send HTTP request using beast
    beast::http::request<beast::http::empty_body> req;
    req.method = "GET";
    req.url = "/";
    req.version = 11;
    req.fields.replace("Host", host + ":" +
        boost::lexical_cast<std::string>(sock.remote_endpoint().port()));
    req.fields.replace("User-Agent", "Beast");
    beast::http::prepare(req);
    beast::http::write(sock, req);

    // Receive and print HTTP response using beast
    beast::streambuf sb;
    beast::http::response<beast::http::streambuf_body> resp;
    beast::http::read(sock, sb, resp);
    std::cout << resp;
}
```

## License

Distributed under the Boost Software License, Version 1.0.
(See accompanying file [LICENSE_1_0.txt](LICENSE_1_0.txt) or copy at
http://www.boost.org/LICENSE_1_0.txt)

## Contact

Please report issues or questions here:
https://github.com/vinniefalco/Beast/issues
