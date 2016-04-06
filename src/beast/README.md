# Beast: A C++ Library

Beast is a collection of libraries, functions, and classes to aid in
certain domains. It is mostly header-only, parts of which depend on Boost.
Beast is written for the most modern version of C++ and tested on the
following compilers:

* Clang 3.x
* GCC 5.x
* Visual Studio 2015

These are some of the libraries contained in Beast:

* **Beast.Asio**

    A collection of utility classes, functions, and traits used when
    building systems using Boost.Asio.

* **Beast.Chrono**

    Offers type-erased virtual and manual clocks for unit tests and
    a seconds-resolution clock for performance.

* **Beast.HTTP**

    Offers programmers simple and performant models of HTTP
    messages and their associated operations including synchronous and
    asynchronous reading and writing using Boost.Asio.

* **Beast.NuDB**

    A high performance embedded key-value database
    engine optimized for use with SSD drives.

* **Beast.UnitTest**

* **Beast.WSProto**

    Provides developers with a robust WebSocket implementation built
    on Boost.Asio with a consistent asynchronous model using a modern
    C++ approach.

* **Containers**

* **`hash_append`**

    An implementation of N3980 ("Types Don't Know #"), providing
    a robust hashing infrastructure supplanting traditional
    implementations of `std::hash`.

    See: http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3980.html

Contains cross platform objects to do a variety of useful things.
No external dependencies, no complicated build steps.

Things people need for building peer to peer, concurrent, cryptographic systems.

The hope is that this will replace the use of boost and other cumbersome jalopies.

## JUCE

Parts of Beast are based on the juce_core module which is provided under the ISC
license. More information about JUCE is available at

http://www.juce.com

## License

Beast is provided under the terms of the ISC license:

        Permission to use, copy, modify, and/or distribute this software for any
        purpose  with  or without fee is hereby granted, provided that the above
        copyright notice and this permission notice appear in all copies.

        THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
        WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
        MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
        ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
        WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
        ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
        OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
