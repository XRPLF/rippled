//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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
*/
//==============================================================================

/** Include this to get the @ref beast_basics module.

    @file beast_basics.h
    @ingroup beast_basics
*/

#ifndef BEAST_BASICS_H_INCLUDED
#define BEAST_BASICS_H_INCLUDED

//------------------------------------------------------------------------------

/*  If you fail to make sure that all your compile units are building Beast with
    the same set of option flags, then there's a risk that different compile
    units will treat the classes as having different memory layouts, leading to
    very nasty memory corruption errors when they all get linked together.
    That's why it's best to always include the BeastConfig.h file before any
    beast headers.
*/
#ifndef BEAST_BEASTCONFIG_H_INCLUDED
# ifdef _MSC_VER
#  pragma message ("Have you included your BeastConfig.h file before including the Beast headers?")
# else
#  warning "Have you included your BeastConfig.h file before including the Beast headers?"
# endif
#endif

//------------------------------------------------------------------------------
/**

@mainpage Beast: A C++ library for server development.

### Version 1.0

Copyright 2008, 2013 by Vinnie Falco \<vinnie.falco@gmail.com\> ([e-mail][0])

Beast is a source code collection of individual modules containing
functionality for a variety of applications, with an emphasis on building
concurrent systems. Beast incorporates parts of [JUCE][3] (Jules' Utility
Class Extensions), available from [Raw Material Software][4]. Beast has no
external dependencies 

Beast is hosted on Github at [https://github.com/vinniefalco/Beast][1]

The online documentation is at [http://vinniefalco.github.com/Beast][2]

## Platforms

All platforms supported by JUCE are also supported by Beast. Currently these
platforms include:

- **Windows**: Applications and VST/RTAS/NPAPI/ActiveX plugins can be built
using MS Visual Studio. The results are all fully compatible with Windows
XP, Vista or Windows 7.

- **Mac OS X**: Applications and VST/AudioUnit/RTAS/NPAPI plugins with Xcode.

- **GNU/Linux**: Applications and plugins can be built for any kernel 2.6 or
later.

- **FreeBSD**: Kernel version 8.4 or higher required.

- **iOS**: Native iPhone and iPad apps.

- **Android**: Supported.

## Prerequisites

This documentation assumes that the reader has a working knowledge of JUCE.
Some modules built on external libraries assume that the reader understands
the operation of those external libraries. Certain modules assume that the
reader understands additional domain-specific information. Modules with
additional prerequisites are marked in the documentation.

## External Modules

Some modules bring in functionality provided by external libraries. For
example, the @ref beast_bzip2 module provides the compression and decompression
algorithms in [bZip2][7]. Usage of these external library modules is optional.
They come with complete source code, as well as options for using either
system or user provided variants of the external libraries: it is not
necessary to download additional source code packages to use these modules.

External code incorporated into Beast is covered by separate licenses. See
the licensing information and notes in the corresponding source files for
copyright information and terms of use.

## Integration

Beast requires recent versions of JUCE. It won't work with versions 1.53 or
earlier. To use the library it is necessary to first download JUCE to a
location where your development environment can find it. Or, you can use your
existing installation of JUCE.

This library uses the same modularized organizational structure as JUCE. To
use a module, first add a path to the list of includes searched by your
development environment or project, which points to the Beast directory. Then,
add the single corresponding .c or .cpp file to your existing project which
already uses JUCE. For example, to use the @ref beast_core module, add the file
beast_core.cpp to your project. Some modules depend on other modules.

To use a module, include the appropriate header from within your source code.
For example, to access classes in the @ref beast_concurrent module, use this:

@code

#include "modules/beast_concurrent/beast_concurrent.h"

@endcode

Then add the corresponding file beast_concurrent.cpp to your build.

## AppConfig

Some Beast features can be controlled at compilation time through
preprocessor directives. The available choices of compilation options are
described in AppConfig.h, located in the AppConfigTemplate directory. Copy
the provided settings into your existing AppConfig.h (a file used by JUCE
convention).

## License

This library contains portions of other open source products covered by
separate licenses. Please see the corresponding source files for specific
terms.

Beast is provided under the terms of The ISC License:

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

Some files contain portions of these external projects, licensed separately:

- [bZip2][7] is Copyright (C) 1996-2010 Julian R Seward. All rights
    reserved. See the corresponding file LICENSE for licensing terms.

- [Soci][13] is Copyright (C) 2004-2008 Maciej Sobczak, Stephen Hutton, and
    various others noted in the corresponding source files. Soci is distributed
    under the [Boost Software License, Version 1.0][14].

- [SQLite][15], placed in the public domain.

[0]: mailto:vinnie.falco@gmail.com "Vinnie Falco (Email)"
[1]: https://github.com/vinniefalco/Beast "Beast Project"
[2]: http://vinniefalco.github.com/Beast/ "Beast Documentation"
[3]: http://rawmaterialsoftware.com/juce.php "JUCE"
[4]: http://rawmaterialsoftware.com/ "Raw Material Software"
[5]: http://www.gnu.org/licenses/gpl-2.0.html "GNU General Public License, version 2"
[6]: http://rawmaterialsoftware.com/jucelicense.php "JUCE Licenses"
[7]: http://www.bzip.org/ "bZip2: Home"
[8]: http://freetype.org/ "The FreeType Project"
[9]: http://www.freetype.org/FTL.TXT "The FreeType Project License"
[10]: http://www.lua.org/ "The Programming Language Lua"
[11]: http://opensource.org/licenses/ISC "The ISC License"
[12]: https://github.com/vinniefalco/LuaBridge
[13]: http://soci.sourceforge.net/ "SOCI"
[14]: http://www.boost.org/LICENSE_1_0.txt "Boost Software License, Version 1.0"
[15]: http://sqlite.org/ "SQLite Home Page"
[16]: http://developer.kde.org/~wheeler/taglib.html "TagLib"
[17]: http://www.gnu.org/licenses/lgpl-2.1.html "Gnu Lesser General Public License, version 2.1"
[18]: http://www.mozilla.org/MPL/1.1/ "Mozilla Public License"

@copyright Copyright 2008-2013 by Vinnie Falco \<vinnie.falco@gmail.com\> ([e-mail][0])
@copyright Provided under the [ISC LIcense][11]
*/

//------------------------------------------------------------------------------

/** Implementation classes.

    Thase classes are used internally.

    @defgroup internal internal
    @internal
*/

//------------------------------------------------------------------------------

/** External modules.

    These modules bring in functionality from third party or system libraries.

    @defgroup external external
*/

//------------------------------------------------------------------------------

/** Core classes.

    This module provides core required functionality, and classes useful for
    general development. All other modules require this module.

    @todo Discuss the treatment of exceptions versus Error objects in the
          library.

    @todo Discuss the additions to BeastConfig.h

    @defgroup beast_core beast_core
*/

/* Get this early so we can use it. */
#include "../beast_core/system/beast_TargetPlatform.h"

//------------------------------------------------------------------------------

#if BEAST_BOOST_IS_AVAILABLE
#include <boost/thread/tss.hpp>
#endif

#ifdef _CRTDBG_MAP_ALLOC
#error "MSVC C Runtime Debug Macros not supported"
#endif

// If the MSVC debug heap headers were included, disable
// the macros during the juce include since they conflict.
#ifdef _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <stdlib.h>
#include <malloc.h>

#pragma push_macro("calloc")
#pragma push_macro("free")
#pragma push_macro("malloc")
#pragma push_macro("realloc")
#pragma push_macro("_recalloc")
#pragma push_macro("_aligned_free")
#pragma push_macro("_aligned_malloc")
#pragma push_macro("_aligned_offset_malloc")
#pragma push_macro("_aligned_realloc")
#pragma push_macro("_aligned_recalloc")
#pragma push_macro("_aligned_offset_realloc")
#pragma push_macro("_aligned_offset_recalloc")
#pragma push_macro("_aligned_msize")

#undef calloc
#undef free
#undef malloc
#undef realloc
#undef _recalloc
#undef _aligned_free
#undef _aligned_malloc
#undef _aligned_offset_malloc
#undef _aligned_realloc
#undef _aligned_recalloc
#undef _aligned_offset_realloc
#undef _aligned_offset_recalloc
#undef _aligned_msize
#endif

#include "../beast_core/beast_core.h"

#ifdef _CRTDBG_MAP_ALLOC
#pragma pop_macro("_aligned_msize")
#pragma pop_macro("_aligned_offset_recalloc")
#pragma pop_macro("_aligned_offset_realloc")
#pragma pop_macro("_aligned_recalloc")
#pragma pop_macro("_aligned_realloc")
#pragma pop_macro("_aligned_offset_malloc")
#pragma pop_macro("_aligned_malloc")
#pragma pop_macro("_aligned_free")
#pragma pop_macro("_recalloc")
#pragma pop_macro("realloc")
#pragma pop_macro("malloc")
#pragma pop_macro("free")
#pragma pop_macro("calloc")
#endif

/** The Beast namespace.

    This namespace contains all Beast symbols.
*/
namespace beast
{

// Order matters

#include "functor/beast_Function.h"
#include "diagnostic/beast_CatchAny.h"
#include "events/beast_OncePerSecond.h"
#include "math/beast_Interval.h"
#include "math/beast_Math.h"
#include "math/beast_MurmurHash.h"
#include "memory/beast_AllocatedBy.h"
#include "memory/beast_RefCountedSingleton.h"
#include "memory/beast_PagedFreeStore.h"
#include "memory/beast_GlobalPagedFreeStore.h"
#include "memory/beast_FifoFreeStoreWithTLS.h"
#include "memory/beast_FifoFreeStoreWithoutTLS.h"
#include "memory/beast_FifoFreeStore.h"
#include "memory/beast_GlobalFifoFreeStore.h"
#include "threads/beast_Semaphore.h"
#include "threads/beast_SerialFor.h"
#include "threads/beast_InterruptibleThread.h"
#include "threads/beast_ReadWriteMutex.h"
#include "threads/beast_ThreadGroup.h"
#include "threads/beast_CallQueue.h"
#include "threads/beast_ConcurrentObject.h"
#include "threads/beast_ConcurrentState.h"
#include "threads/beast_GlobalThreadGroup.h"
#include "threads/beast_Listeners.h"
#include "threads/beast_ManualCallQueue.h"
#include "threads/beast_ParallelFor.h"
#include "threads/beast_ThreadWithCallQueue.h"
#include "threads/beast_SharedObject.h"

}

#endif

