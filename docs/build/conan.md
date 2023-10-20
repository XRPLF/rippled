## A crash course in CMake and Conan

To better understand how to use Conan,
we should first understand _why_ we use Conan,
and to understand that,
we need to understand how we use CMake.


### CMake

Technically, you don't need CMake to build this project.
You could manually compile every translation unit into an object file,
using the right compiler options,
and then manually link all those objects together,
using the right linker options.
However, that is very tedious and error-prone,
which is why we lean on tools like CMake.

We have written CMake configuration files
([`CMakeLists.txt`](./CMakeLists.txt) and friends)
for this project so that CMake can be used to correctly compile and link
all of the translation units in it.
Or rather, CMake will generate files for a separate build system
(e.g. Make, Ninja, Visual Studio, Xcode, etc.)
that compile and link all of the translation units.
Even then, CMake has parameters, some of which are platform-specific.
In CMake's parlance, parameters are specially-named **variables** like
[`CMAKE_BUILD_TYPE`][build_type] or
[`CMAKE_MSVC_RUNTIME_LIBRARY`][runtime].
Parameters include:

- what build system to generate files for
- where to find the compiler and linker
- where to find dependencies, e.g. libraries and headers
- how to link dependencies, e.g. any special compiler or linker flags that
    need to be used with them, including preprocessor definitions
- how to compile translation units, e.g. with optimizations, debug symbols,
    position-independent code, etc.
- on Windows, which runtime library to link with

For some of these parameters, like the build system and compiler,
CMake goes through a complicated search process to choose default values.
For others, like the dependencies,
_we_ had written in the CMake configuration files of this project
our own complicated process to choose defaults.
For most developers, things "just worked"... until they didn't, and then
you were left trying to debug one of these complicated processes, instead of
choosing and manually passing the parameter values yourself.

You can pass every parameter to CMake on the command line,
but writing out these parameters every time we want to configure CMake is
a pain.
Most humans prefer to put them into a configuration file, once, that
CMake can read every time it is configured.
For CMake, that file is a [toolchain file][toolchain].


### Conan

These next few paragraphs on Conan are going to read much like the ones above
for CMake.

Technically, you don't need Conan to build this project.
You could manually download, configure, build, and install all of the
dependencies yourself, and then pass all of the parameters necessary for
CMake to link to those dependencies.
To guarantee ABI compatibility, you must be sure to use the same set of
compiler and linker options for all dependencies _and_ this project.
However, that is very tedious and error-prone, which is why we lean on tools
like Conan.

We have written a Conan configuration file ([`conanfile.py`](../../conanfile.py))
so that Conan can be used to correctly download, configure, build, and install
all of the dependencies for this project,
using a single set of compiler and linker options for all of them.
It generates files that contain almost all of the parameters that CMake
expects.
Those files include:

- A single toolchain file.
- For every dependency, a CMake [package configuration file][pcf],
    [package version file][pvf], and for every build type, a package
    targets file.
    Together, these files implement version checking and define `IMPORTED`
    targets for the dependencies.

The toolchain file itself amends the search path
([`CMAKE_PREFIX_PATH`][prefix_path]) so that [`find_package()`][find_package]
will [discover][search] the generated package configuration files.

**Nearly all we must do to properly configure CMake is pass the toolchain
file.**
What CMake parameters are left out?
You'll still need to pick a build system generator,
and if you choose a single-configuration generator,
you'll need to pass the `CMAKE_BUILD_TYPE`,
which should match the `build_type` setting you gave to Conan.

Even then, Conan has parameters, some of which are platform-specific.
In Conan's parlance, parameters are either settings or options.
**Settings** are shared by all packages, e.g. the build type.
**Options** are specific to a given package, e.g. whether to build and link
OpenSSL as a shared library.

For settings, Conan goes through a complicated search process to choose
defaults.
For options, each package recipe defines its own defaults.

You can pass every parameter to Conan on the command line,
but it is more convenient to put them in a configuration file, once, that
Conan can read every time it is configured.
For Conan, that file is a [profile][profile].
**All we must do to properly configure Conan is edit and pass the profile.**
By default, Conan will use the profile named "default".
