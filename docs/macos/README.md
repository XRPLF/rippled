#  build rippled server software from source

`rippled` written in c++ is the server software that powers the xrp ledger and runs on a variety of differing platforms.  the `rippled` server software can run in several modes depending on its configuration, here are instructions on how to build `rippled` from source as well as learn how cmake and conan behave in the compilation process.

####  contents

1.  [references](#references)
2.  [systems hardware and software configuration](#systems-hardware-and-software-configuration)
3.  [dependencies](#dependencies) 
4.  [cmake](#cmake)
5.  [conan](#conan)
6.  [`~/.conan/profiles/default`](#conan-profiles-default)
7.  [cmake and conan](#cmake-and-conan)
8.  [package setup commands](#package-set-up-commands)
9.  [build commands](#build-commands)
10. [`rippled/conanfile.py`](#rippled-conanfile-py)
11. [running the binary executable](#running-the-binary-executable)
12. [console log output from commands](#console-log-output-from-commands)

---------------------------------------------------------------------------------------------------

##  references

1.  [server modes](https://xrpl.org/rippled-server-modes.html)
2.  [master build instructions](https://github.com/XRPLF/rippled/blob/master/BUILD.md#a-crash-course-in-cmake-and-conan)

##  systems hardware and software configuration

1.  **OS** macOS 13.4 22F66 arm64
2.  **host** MacBookPro18,3
3.  **kernel** 22.5.0
4.  **packages** 56 (brew)
5.  **shell** zsh 5.9
6.  **cpu** Apple M1 Pro
7.  **memory** 2026MiB / 16384MiB

##  dependencies

1.  [`gcc`](https://github.com/Homebrew/homebrew-core/blob/HEAD/Formula/gcc.rb) gnu compiler collection **version 13.0.0**

2.  [`clang`](https://opensource.apple.com/source/clang/clang-23/clang/tools/clang/docs/UsersManual.html) apple clang **version 14.0.3**

3.  [`cmake`](https://formulae.brew.sh/formula/cmake) cross platform make **version 3.26.4**

4.  [`conan`](https://conan.io/downloads.html) conan package manager **version 1.59.0**

##  cmake

Technically, CMake is not required to build rippled. You could manually compile every translation unit into an object file using the correct compiler options, and then manually link all those objects together with the right linker options. However, this approach is tedious and error-prone, which is why CMake is used. CMake configuration files have been written for this project, allowing CMake to correctly compile and link all the translation units or generate files for a separate build system to handle compilation and linking.  

CMake is an extensible system that manages the build process in an operating system and compiler-independent manner. Unlike many cross-platform systems, CMake is designed to be used with the native build environment. Simple configuration files called `CMakeLists.txt` placed in each source directory are used to generate standard build files, such as makefiles on Unix.  it controls the software compilation process using platform and compiler-independent configuration files.

CMake can generate a native build environment that compiles source code, creates libraries, generates wrappers, and builds executables in various combinations. It supports in-place and out-of-place builds, allowing multiple builds from a single source tree. CMake also supports static and dynamic library builds. During its operation, CMake locates files, libraries, and executables, and may encounter optional build directives. This information is gathered into the cache, which can be modified by the user before generating the native build files.  CMake is designed to support complex directory hierarchies and applications dependent on multiple libraries. For example, it supports projects consisting of multiple toolkits, where each toolkit contains several directories, and the application depends on the toolkits along with additional code. CMake can also handle situations where executables must be built in a specific order to generate code that is then compiled and linked into a final application.

The build process is controlled by creating one or more `CMakeLists.txt` files in each directory, including subdirectories, that make up the project. Each `CMakeLists.txt` file consists of one or more commands in the form `COMMAND` (args..), where COMMAND is the name of the command, and args is a whitespace-separated list of arguments.

CMake has parameters, some of which are platform-specific, referred to as variables include `CMAKE_BUILD_TYPE` or `CMAKE_MSVC_RUNTIME_LIBRARY`.

here are some key points to know:

1.  `CMakeLists.txt` is the main configuration file written in cmake scipting language.  it specifies the project structre, source files, dependencies, build options, and targets. 

2.  **generators**  cmake supports various build system generators such as make, ninja, visual studio code, and xcode.  these generators produce build files specific to these respective systems.

3.  **variables** cmake provides various variables that control the build process, such as the build data type `CMAKE_BUILD_TYPE`, compiler options, dependency paths, etc.  these variables can be set in `CMakeLists.txt` or passed via the commandline.

4.  **toolchain file** a toolchain file is used to specify platform specific settings, including the compiler, linker, and other tool locations.  it helps ensure consistent configuration across different platforms.

5.  **build configuration**  cmake supports different build configurations, such as debug, release, relwithwebinfo, etc.  these configurations determine compilation flags, optimization levels, and other settings used during the build process.

##  conan

conan is a package manager designed for c++, aimed at simplifying the process of software package management which includes installation, upgrade, configuration, and management of software packages or libraries.  conan is platform independent and the recipes developed under conan can function across all platforms, configuration, and compilers.

understanding conan involves getting to grips with several key components

1.  `Conanfile.py`  this is the configuration file that conan uses to defien and manage project dependencies.  it provides critical information such as the package name, version, build options, and dependencies.

2.  **profiles** in conan profiles are used to define the settings and options for your build enviroment.  these include variables like the compiler version, build type, and options specific to your platform.  profiles ensure consistent builds across various machines.

the profile collectivly holds definitions for the build enviroment.  it holds the target platform, operating system, architecture, compiler, build type, and language standard.  additionally there are set flags and env variables related to the boost library and specify the paths to c and c++ compilers used for the build.

3.  **packages**  conan packages represent external libraries or dependencies on which the project depends.  package recipes provide instructions on how to download, configure, build, and install these dependencies.

4.  **build folders** conan employs build folders to segregate different builds and avoid conflicts among dependencies.  while conan creates build files in the current directory by default, users can specify a different build folder to maintain a clean project directory and avoid pollution.

`Conanfile.py` enables conan to efficiently download, configure, build, and install all dependencies.  this process utilizes a unified set of compiler and linker options for all dependencies.  the `Conanfile.py` generates files incorporating most parameters that Cmake expects.  these include a single toolchain file, and for each dependency files that collectively implements version checking and define imported targets for dependencies.  these files are a cmake package configuration file, a package version file, and a package target file for every build type.

the toolchain modifies the search path `CMAKE_PREFIX_PATH` to facilitat `find_package()` in locating the generated package configuration files.  to configure cmake properly one needs only pass the toolchain file.

however it's important to note that cmake parameters are excluded, you must select a build system generator and if choosing a single configuration generator specify the `CMAKE_BUILD_TYPE` which should align with the `build_type` setting given in `Conanfile.py`.  parameters are either `setting` or `options`, and settings are shared among all packages, for example build type.  for settings conan understakes an intricate search process to establish defaults.  although you can pass every parameter to conan via cli, it's most convient to store them within a profile. (profiles will be discussed in depth further in this documentation). 

[`~/.conan/profiles/default`](#conan-profiles-default) will be created within your local root directory. and will look something like this after package setup.

##  `CMakeLists.txt`

<details><summary>view</summary>
<code>cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

# Fix "unrecognized escape" issues when passing CMAKE_MODULE_PATH on Windows.
file(TO_CMAKE_PATH "${CMAKE_MODULE_PATH}" CMAKE_MODULE_PATH)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/Builds/CMake")

project(rippled)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# make GIT_COMMIT_HASH define available to all sources
find_package(Git)
if(Git_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --abbrev=40
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE gch)
    if(gch)
        set(GIT_COMMIT_HASH "${gch}")
        message(STATUS gch: ${GIT_COMMIT_HASH})
        add_definitions(-DGIT_COMMIT_HASH="${GIT_COMMIT_HASH}")
    endif()
endif() #git

if(thread_safety_analysis)
  add_compile_options(-Wthread-safety -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS -DRIPPLE_ENABLE_THREAD_SAFETY_ANNOTATIONS)
  add_compile_options("-stdlib=libc++")
  add_link_options("-stdlib=libc++")
endif()

include (CheckCXXCompilerFlag)
include (FetchContent)
include (ExternalProject)
include (CMakeFuncs) # must come *after* ExternalProject b/c it overrides one function in EP
include (ProcessorCount)
if (target)
  message (FATAL_ERROR "The target option has been removed - use native cmake options to control build")
endif ()

include(RippledSanity)
include(RippledVersion)
include(RippledSettings)
include(RippledRelease)
# this check has to remain in the top-level cmake
# because of the early return statement
if (packages_only)
  if (NOT TARGET rpm)
    message (FATAL_ERROR "packages_only requested, but targets were not created - is docker installed?")
  endif()
  return ()
endif ()
include(RippledCompiler)
include(RippledInterface)

option(only_docs "Include only the docs target?" FALSE)
include(RippledDocs)
if(only_docs)
  return()
endif()

###

include(deps/Boost)
find_package(OpenSSL 1.1.1 REQUIRED)
set_target_properties(OpenSSL::SSL PROPERTIES
  INTERFACE_COMPILE_DEFINITIONS OPENSSL_NO_SSL2
)
add_subdirectory(src/secp256k1)
add_subdirectory(src/ed25519-donna)
find_package(lz4 REQUIRED)
# Target names with :: are not allowed in a generator expression.
# We need to pull the include directories and imported location properties
# from separate targets.
find_package(LibArchive REQUIRED)
find_package(SOCI REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(Snappy REQUIRED)

option(rocksdb "Enable RocksDB" ON)
if(rocksdb)
  find_package(RocksDB REQUIRED)
  set_target_properties(RocksDB::rocksdb PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS RIPPLE_ROCKSDB_AVAILABLE=1
  )
  target_link_libraries(ripple_libs INTERFACE RocksDB::rocksdb)
endif()

find_package(nudb REQUIRED)
find_package(date REQUIRED)
include(deps/Protobuf)
include(deps/gRPC)

target_link_libraries(ripple_libs INTERFACE
  ed25519::ed25519
  LibArchive::LibArchive
  lz4::lz4
  nudb::core
  OpenSSL::Crypto
  OpenSSL::SSL
  Ripple::grpc_pbufs
  Ripple::pbufs
  secp256k1::secp256k1
  soci::soci
  SQLite::SQLite3
)

if(reporting)
  find_package(cassandra-cpp-driver REQUIRED)
  find_package(PostgreSQL REQUIRED)
  target_link_libraries(ripple_libs INTERFACE
    cassandra-cpp-driver::cassandra-cpp-driver
    PostgreSQL::PostgreSQL
  )
endif()

###

include(RippledCore)
include(RippledInstall)
include(RippledCov)
include(RippledMultiConfig)
include(RippledValidatorKeys)</code>
<details>

##  `CMAKE_PREFIX_PATH`

**parameters include**

-  what build system to generate files for
-  where to find the compiler and linker
-  where to find dependencies, e.g. libraries and headers
-  how to link dependencies e.g. any special compiler or linker flags that need to be used with them, including preprocessor definition
-  how to compile translation units with optimizations, debug symbols, position independent code, etc

for some of these parameters, like the build system and compiler, cmake goes through a complicated search process to choose default values.  for other like the dependencies, we had written in the cmake config files of this project to our own complicated process to choose defaults.

you can pass every parameter to cmake on the command line, but writing out these parameters everytime we want to configure cmake is a pain.  once you configure a file once cmake can read everytime it is configured, which is a toolchain file.

a toolchain is a set of utilities to compile, link libraries, and creat archives, and other tasks to drive the build.  the toolchain utilities available are determined by the languages enabled.

##  `CMAKE_BUILD_TYPE`

`CMAKE_BUILD_TYPE` must match `build_type`.  `CMAKE_BUILD_TYPE` is a cmake variable that defines the build type or configuration for your cmake project.  it allows you to specify different build configuration such as debug, release, or custom configurations specific to your project.   if you dont specify the `CMAKE_BUILD_TYPE` var, cmake uses an empty string as the build type.  

in this case the generated build system such as Makefiles or VSC project may use its default build configuration, which varies depending on the system or generator.   

##  cmake and conan

in order to use cmake and conan together, you will need to configure cmake to recognize and link the dependencies managed by conan.  conan generates package configuration files that cmake can use to discover and link dependencies correctly.  the typical workflow involves exporting the dependencies using conant, creating a build directory, installing the dependencies using conan, configuring cmake with the generated package configuration file, and finally building the project.

1.  exporting dependencies
2.  creating build directory
3.  installing dependencies
4.  configuring cmake
5.  building the project

##  package set up commands

before we start we need to ensure to define the directory paths of our compiler executables, these will be included in the default script.  the following commands will result in a scripted config found in the default script generated from conan [`~/.conan/profiles/default`](#conan-profiles-default)  the format of most calls are as follows `conan profile update <option>=<value> <profile_name>`, the name of profile will be in our case default.  however it can be named anything.  the result from `which gcc` and `which g++` will need to be used to initialize paths, in our case `which gcc` results in `/usr/bin/gcc` and `which g++` results in `/usr/bin/g++` and have been defined in some of the commands.

1.  `pwd rippled`

2.  `which gcc` 

3.  `which g++`

4.  `git checkout master`

5.  `conan profile new default --detect`

once completed the following profile should reflect that of your own.  once a profile is created, it can be used in a build, the conan install command downloads or builds the necessary packages according to the settings specified in the profile.  when calling `conan profile new default --detect` your shell should return a message declaring the location of the module within the `conan` profiles directory as a text file within your local system.

```
❯ conan profile new default --detect
Found apple-clang 14.0
apple-clang>=13, using the major as version
Profile created with detected settings: /Users/.conan/profiles/default
```

6.  `conan profile update settings.compiler.cppstd=20 default`

7.  `conan profile update env.CC=/usr/bin/gcc default`

8.  `conan profile update env.CFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1 default`

9.  `conan profile update env.cxx=/usr/bin/g++ default`

10. `conan profile update env.CXXFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1 default`

11.  `conan profile update 'conf.tools.build:compiler_executables={"c": "/usr/bin/gcc", "cpp": "/usr/bin/g++"}' default`

12.  `conan profile update options.boost:extra_b2_flags="define=BOOST_ASIO_HAS_STD_INVOKE_RESULT" default`


##  `~/.conan/profiles/default`(#conan-profiles-default)

```
[settings]
os=Macos
os_build=Macos
arch=armv8
arch_build=armv8
compiler=apple-clang
compiler.version=14
compiler.libcxx=libc++
build_type=Release
compiler.cppstd=20
[options]
boost:extra_b2_flags=define=BOOST_ASIO_HAS_STD_INVOKE_RESULT
[build_requires]
[env]
CC=/usr/bin/gcc
CFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1
CXX=/usr/bin/g++
CXXFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1
[conf]
tools.build:compiler_executables={'c': '/usr/bin/gcc', 'cpp': '/usr/bin/g++'}
```

##  build commands

1.  `pwd rippled`

2.  `conan export external/snappy snappy/1.1.9@`

export our conan recipe for snappy, this doesnt explicitly link the c++ standard library, which allows us to statically link it.  snappy is a fast compression/decompression library developed by google, it aims to provide high speed data processing iwth a reasonable compression ratio (i do not have knowledge in compression/decompression tools however ill just blockbox).  `conanfile.py` is exported to snappy which is located in the `external/snappy` directory to the local conan cache.  and `snappy/1.1.9@` is the reference for the recipe in the local conan cache.  `rippled/external/snappy/`

3.  `mkdir .build`

4.  `cd .build`

5.  `conan install .. --output-folder . --build missing --settings build_type=Release`

6.  `cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=/build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..`

7.  `cmake --build .` 

9.  `./rippled --version`

8.  `./rippled --unittest` 

upon successfully configuring conan and cmake a console log will be provided along the lines of

```
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_tollchain.cmake -DCMAKE_BUILD_TYPE=Release ..

-- Configuring done (1.4s)
-- Generating done (0.1s)
-- Build files have been written to: /Users/mbergen/Documents/Github/rippled/.build
```

##  reset conan package setup

`reset_conan.sh`, has scripts containing commands to reset your conan profile.  ensure to call `which conan` in order to determine your `.conan` directory is not located in your local machine's root directory.

1.  `chmod +x reset_conan.sh` to make the script executable

2.  `./reset_conan.sh`

```bash
`pwd rippled`
`rm -rf ~/.conan/data`
`rm -rf ~/.conan/conan.conf`
`rm -rf ~/.conan/profiles`
`rm -r .build`
```

##  command walkthrough

###  0.  `pwd rippled`

starting in ripple root

###  1.  `which gcc` 

`which` locate a program in user's path `gcc`

###  2.  `which g++`

run the following command to find the installation path of gcc using `which` token the output will display path `/usr/bin/gcc/<path>` make a note of this path.  use this path as the values for `env.CC=`, `env.FLAGS`, `c`, `cpp` variables.

###  3.  `git checkout master`

this guide has been written for `master` in june of 2023

###  4.  `conan profile new default --detect`

these settings define various aspects of the build process, such as the operating system, architecture, compiler.  boost library flags, and environment variables.  

`[settings]` this section specifies the settings related to the target platform, operating system, architecture, and compiler used for the rippled build.

- `os` indicates the target operating system used which is macos
- `os_build` represents the operating system used for building the rippled software, also macos. this is defined for rippled being built on a different os than the target platform, these could be `Linux` what have you. having `os_build` and `os` allows for cross-compilation, where the software is built on one operating system but targeted for another.
-  `arch` specifies the target architecture, which is `armv8` ARM64 in this case.
-  `arch_build` denotes the architecture used for the build process, which is `armv8` as well
-  `compiler` indicates the compiler used, in this case it's apple clang
-  `compiler.version` specifies version of compiler
-  `compiler.libcxx` indicates the c++ standard library used, which is `libc++`
-  `build_type` specifies the build type which is release in our case
-  `compiler.cppstd` specifies the c++ language standard used
-  `[build_requires]` uninitialized but is used to specify any needed dependencies needed during process
-  `CC` specifies the path to the c compiler which is `/usr/bin/gcc`
-  `CFLAGS` sets the compiler flags for the c compiler and adds the `DBOOST_ASIN_HAS_STD_INVOKE_RESULT=1` flag and defines the macro to 1
-  `CXX` specifies the path to the c++ compiler
-  `CXXFLAGS` sets the compiler flags for the c++ compiler and adds the `-BOOST_ASIO_HAS_STD_INVOKE_RESULT=1`
-  `[conf]` is used for configuration options
-  `tools.build:compiler_executables` option is used to specify the paths to the c and cpp compilers that will be used during the compilation process so objects can be linked successfully.
-  in this case the configuration has set the `compiler_executables` option to a dictionary that maps the compiler names to their corresponding executable paths (note these paths are examples and may look different depending on your system) 
    - `c` specifies that the c compiler executable can be found in `/usr/bin/gcc`
    - `cpp` specifies that the cpp compiler executable can be found `/usr/bin/g++`

```
[settings]
os=Macos
os_build=Macos
arch=armv8
arch_build=armv8
compiler=apple-clang
compiler.version=14
compiler.libcxx=libc++
build_type=Release
compiler.cppstd=20
[options]
boost:extra_b2_flags=define=BOOST_ASIO_HAS_STD_INVOKE_RESULT
[build_requires]
[env]
CC=/usr/bin/gcc
CFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1
CXX=/usr/bin/g++
CXXFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1
[conf]
tools.build:compiler_executables={'c': '/usr/bin/gcc', 'cpp': '/usr/bin/g++'}
```
###  5.  `conan profile update settings.compiler.cppstd=20 default`

-  `conan` calling the package manager to execute
-  `profile` specifying we are working with a conan profile specifically the profile created with detected settings returned from step 4 [conan profile new default --detect](#conan-profile-new-default---detect).  
-  `update` indicates that we want to update an existing profile 
-  `settings.compiler.cppstd` refers to which will be initialized under `[settings]` under the `20` dialect of the c++20 standard.
-  `default` is the destination profile in this case its `default`

###  6.  `conan profile update env.CC=/usr/bin/gcc default`


###  7.  `conan profile update env.CFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1 default`
###  8.  `conan profile update env.CXX=/usr/bin/g++ default`
###  9.  `conan profile update env.CXXFLAGS=-DBOOST_ASIO_HAS_STD_INVOKE_RESULT=1 default`
###  10.  `conan profile update 'conf.tools.build:compiler_executables={"c": "/usr/bin/gcc", "cpp": "/usr/bin/g++"}' default`
###  
###  11.  `conan profile update -o boost:extra_b2_flags="define=BOOST_ASIO_HAS_STD_INVOKE_RESULT"`

###  12.  `pwd rippled`

###  13.  `conan export external/snappy snappy/1.1.9@`

export our conan recipe for snappy, this doesnt explicitly link the c++ standard library, which allows us to statically link it.

snappy is a fast compression/decompression library developed by google, it aims to provide high speed data processing iwth a reasonable compression ratio (i do not have knowledge in compression/decompression tools however ill just blockbox).  `conanfile.py` is exported to snappy which is located in the `external/snappy` directory to the local conan cache.  and `snappy/1.1.9@` is the reference for the recipe in the local conan cache.  `rippled/external/snappy/`

###  14.  `mkdir .build`

###  15.  `cd .build`

by default the install folder is your current working directory.  if you don't move into your build directory before calling Conan, then you may see it polluting your project root directory with these files  to make conan put them in your build directory youll have to add the options `--install-folder` or `-if` to every `conan install` command

###  16.  `conan install .. --output-folder . --build missing --settings build_type=Release`

1.  `conan install ..` tells conan to install the dependencies listed in the `conanfile.py`
2.  `--output-folder .` this argument is specifying that the output from this command should be placed in the current directory which is `.`
3.  `--build missing` tells conan to build any dependencies that are missing from conan's cache, essentially the cache is located here `~/.conan/conan.conf` and will be written out under the `[storage]` section 
4.  `--settings build_type=Release` argument tells conan to build the dependencies for a release build

###  17.  `cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=/build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..`

1.  `cmake` command to run the cmake tool which is a build system generator whcih reads the `CMakeLists.txt` file that you write and generate build files for a build tool of your choice like Make

2.  `-D` is used to define a variable that will be passed into the CMake script so `-DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake` is telling CMake to use a specific toolchain file which should exist after running the previous commands before 4

a toolchain file is a script that cmake reads before your main `CMakeLists.txt` and is used to set up the compiler and other tools like linkers that will be used to build the project.  the toolchain file is often used when cross-compiling which is wehn  you are building code on one type of system (the host), such that it can be executed on a different type of system (the target).  this is common when you have limited resources.  the cross compiler produces binaries that can be executed on a specific argitecture.  CMAKE's toolchain file handles all of these complexities and specifies the deatils about the target system and the cross compiler to be used, which cmake then uses when generating the build files.

3.  `conan_toolchain.cmake` is a toolchain file that was generated by conan.  it sets up the tools and settinsg taht conan has figure out for the project based on the dep and settings in `conanfile.py`

4.  `-DCMAKE_BUILD_TYPE=Release` sets the build type to `Release`, this typically means that the code will be optimized for speed and the debug info will be removed making the binaries smaller!!  

5.  `..` at the end of the command is specifying the source directory of the project which is the parent directory of build which were currently in when running the 4th command.

**Note**  ensure to run cmake from the `.build` directory, to maintain [modularity](https://en.wikipedia.org/wiki/Modular_programming)

###  18.  `cmake --build .` 

final invokation for the underlying build system to compile into the unix binary executable `rippled`.

###  19.  `./rippled --version`
###  20.  `./rippled --unittest` 

execute the `rippled` unix binary executable and `--unittest` argument means to run the program's unit tests.  unit tests are small isolated tests that check the functionality of a specific part of a program.  

###  console log output from commands



[ 99%] Building CXX object CMakeFiles/rippled.dir/src/test/shamap/SHAMapSync_test.cpp.o
[100%] Building CXX object CMakeFiles/rippled.dir/src/test/shamap/SHAMap_test.cpp.o
[100%] Building CXX object CMakeFiles/rippled.dir/src/test/unit_test/multi_runner.cpp.o
[100%] Linking CXX executable rippled
[100%] Built target rippled

##  running the binary executable

```
rippled [options] <command> <params>

General Options:
  --conf arg             Specify the configuration file.
  --debug                Enable normally suppressed debug logging
  -h [ --help ]          Display this message.
  --newnodeid            Generate a new node identity for this server.
  --nodeid arg           Specify the node identity for this server.
  --quorum arg           Override the minimum validation quorum.
  --reportingReadOnly    Run in read-only reporting mode
  --silent               No output to the console after startup.
  -a [ --standalone ]    Run with no peers.
  -v [ --verbose ]       Verbose logging.
  --version              Display the build version.

RPC Client Options:
  --rpc                  Perform rpc command - see below for available
                         commands. This is assumed if any positional parameters
                         are provided.
  --rpc_ip arg           Specify the IP address for RPC command. Format:
                         <ip-address>[':'<port-number>]
  --rpc_port arg         DEPRECATED: include with rpc_ip instead. Specify the
                         port number for RPC command.

Ledger/Data Options:
                         [import_db] configuration file section) into the
                         current node database (specified in the [node_db]
                         configuration file section).
  --ledger arg           Load the specified ledger and start from the value
                         given.
  --ledgerfile arg       Load the specified ledger file.
  --load                 Load the current ledger from the local DB.
  --net                  Get the initial ledger from the network.
  --nodetoshard          Import node store into shards
  --replay               Replay a ledger close.
  --start                Start from a fresh Ledger.
  --startReporting arg   Start reporting from a fresh Ledger.
  --vacuum               VACUUM the transaction db.
  --valid                Consider the initial ledger a valid network ledger.

Unit Test Options:
  -q [ --quiet ]         Suppress test suite messages, including suite/case
                         name (at start) and test log messages.
  -u [ --unittest ] arg  Perform unit tests. The optional argument specifies
                         one or more comma-separated selectors. Each selector
                         specifies a suite name, full-name (lib.module.suite),
                         module, or library (checked in that order).
  --unittest-arg arg     Supplies an argument string to unit tests. If
                         provided, this argument is made available to each
                         suite that runs. Interpretation of the argument is
                         handled individually by any suite that accesses it --
                         as such, it typically only make sense to provide this
                         when running a single suite.
  --unittest-ipv6        Use IPv6 localhost when running unittests (default is
                         IPv4).
  --unittest-log         Force unit test log message output. Only useful in
                         combination with --quiet, in which case log messages
                         will print but suite/case names will not.
  --unittest-jobs arg    Number of unittest jobs to run in parallel (child
                         processes).

Commands:
     account_currencies <account> [<ledger>] [strict]
     account_info <account>|<seed>|<pass_phrase>|<key> [<ledger>] [strict]
     account_lines <account> <account>|"" [<ledger>]
     account_channels <account> <account>|"" [<ledger>]
     account_objects <account> [<ledger>] [strict]
     account_offers <account>|<account_public_key> [<ledger>] [strict]
     account_tx accountID [ledger_index_min [ledger_index_max [limit ]]] [binary]
     book_changes [<ledger hash|id>]
     book_offers <taker_pays> <taker_gets> [<taker [<ledger> [<limit> [<proof> [<marker>]]]]]
     can_delete [<ledgerid>|<ledgerhash>|now|always|never]
     channel_authorize <private_key> <channel_id> <drops>
     channel_verify <public_key> <channel_id> <drops> <signature>
     connect <ip> [<port>]
     consensus_info
     deposit_authorized <source_account> <destination_account> [<ledger>]
     download_shard [[<index> <url>]]
     feature [<feature> [accept|reject]]
     fetch_info [clear]
     gateway_balances [<ledger>] <issuer_account> [ <hotwallet> [ <hotwallet> ]]
     get_counts
     json <method> <json>
     ledger [<id>|current|closed|validated] [full]
     ledger_accept
     ledger_cleaner
     ledger_closed
     ledger_current
     ledger_request <ledger>
     log_level [[<partition>] <severity>]
     logrotate
     manifest <public_key>
     node_to_shard [status|start|stop]
     peers
     ping
     random
     peer_reservations_add <public_key> [<description>]
     peer_reservations_del <public_key>
     peer_reservations_list
     ripple ...
     ripple_path_find <json> [<ledger>]
     server_info [counters]
     server_state [counters]
     sign <private_key> <tx_json> [offline]
     sign_for <signer_address> <signer_private_key> <tx_json> [offline]
     stop
     submit <tx_blob>|[<private_key> <tx_json>]
     submit_multisigned <tx_json>
     tx <id>
     validation_create [<seed>|<pass_phrase>|<key>]
     validator_info
     validators
     validator_list_sites
     version
     wallet_propose [<passphrase>]
```

[`./rippled --unittest`]

the console log output from `./rippled --unittest` is the result of the built-in unit tests being run.  each line is the name of a specific test case or test suite being executed. the end of the test will output performance results

```
./rippled --unittest
....
ripple.tx.Ticket Sign with TicketSequence
ripple.tx.Ticket Fix both Seq and Ticket

Longest suite times:
71.8s ripple.tx.NFToken
54.4s ripple.tx.NFTokenBurn
42.8s ripple.tx.Offer
34.9s ripple.app.ValidatorSite
27.3s ripple.app.ShardArchiveHandler
23.3s ripple.app.TheoreticalQuality
14.6s ripple.app.Flow
13.2s ripple.app.AccountDelete
8.3s ripple.tx.Check
7.9s ripple.app.LedgerReplayer
451.4s, 205 suites, 1654 cases, 577987 tests total, 0 failures
```
