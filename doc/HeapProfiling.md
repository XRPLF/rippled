## Heap profiling of rippled with jemalloc

The jemalloc library provides a good API for doing heap analysis,
including a mechanism to dump a description of the heap from within the
running application via a function call. Details on how to perform this
activity in general, as well as how to acquire the software, are available on
the jemalloc site:
[https://github.com/jemalloc/jemalloc/wiki/Use-Case:-Heap-Profiling](https://github.com/jemalloc/jemalloc/wiki/Use-Case:-Heap-Profiling)

jemalloc is acquired separately from rippled, and is not affiliated
with Ripple Labs. If you compile and install jemalloc from the
source release with default options, it will install the library and header
under `/usr/local/lib` and `/usr/local/include`, respectively. Heap
profiling has been tested with rippled on a Linux platform. It should
work on platforms on which both rippled and jemalloc are available.

To link rippled with jemalloc, the argument
`profile-jemalloc=<jemalloc_dir>` is provided after the optional target.
The `<jemalloc_dir>` argument should be the same as that of the
`--prefix` parameter passed to the jemalloc configure script when building.

## Examples:

Build rippled with jemalloc library under /usr/local/lib and
header under /usr/local/include:

    $ scons profile-jemalloc=/usr/local

Build rippled using clang with the jemalloc library under /opt/local/lib
and header under /opt/local/include:

    $ scons clang profile-jemalloc=/opt/local

----------------------

## Using the jemalloc library from within the code

The `profile-jemalloc` parameter enables a macro definition called
`PROFILE_JEMALLOC`. Include the jemalloc header file as
well as the api call(s) that you wish to make within preprocessor
conditional groups, such as:

In global scope:

    #ifdef PROFILE_JEMALLOC
    #include <jemalloc/jemalloc.h>
    #endif

And later, within a function scope:

    #ifdef PROFILE_JEMALLOC
    mallctl("prof.dump", NULL, NULL, NULL, 0);
    #endif

Fuller descriptions of how to acquire and use jemalloc's api to do memory
analysis are available at the [jemalloc
site.](http://www.canonware.com/jemalloc/)

Linking against the jemalloc library will override
the system's default `malloc()` and related functions with jemalloc's
implementation. This is the case even if the code is not instrumented
to use jemalloc's specific API.

