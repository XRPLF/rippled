# Visual Studio 2017 Build Instructions

## Important

We do not recommend Windows for rippled production use at this time. Currently,
the Ubuntu platform has received the highest level of quality assurance,
testing, and support. Additionally, 32-bit Windows versions are not supported.

## Prerequisites

To clone the source code repository, create branches for inspection or
modification, build rippled under Visual Studio, and run the unit tests you will
need these software components

| Component | Minimum Recommended Version |
|-----------|-----------------------|
| [Visual Studio 2017](README.md#install-visual-studio-2017)| 15.5.4 |
| [Git for Windows](README.md#install-git-for-windows)| 2.16.1 |
| [OpenSSL Library](README.md#install-openssl) | 1.1.1L |
| [Boost library](README.md#build-boost) | 1.70.0 |
| [CMake for Windows](README.md#optional-install-cmake-for-windows)* | 3.12 |

\* Only needed if not using the integrated CMake in VS 2017 and prefer generating dedicated project/solution files.

## Install Software

### Install Visual Studio 2017

If not already installed on your system, download your choice of installer from
the [Visual Studio 2017
Download](https://www.visualstudio.com/downloads/download-visual-studio-vs)
page, run the installer, and follow the directions. **You may need to choose the
`Desktop development with C++` workload to install all necessary C++ features.**

Any version of Visual Studio 2017 may be used to build rippled. The **Visual
Studio 2017 Community** edition is available free of charge (see [the product
page](https://www.visualstudio.com/products/visual-studio-community-vs) for
licensing details), while paid editions may be used for an initial free-trial
period.

### Install Git for Windows

Git is a distributed revision control system. The Windows version also provides
the bash shell and many Windows versions of Unix commands. While there are other
varieties of Git (such as TortoiseGit, which has a native Windows interface and
integrates with the Explorer shell), we recommend installing [Git for
Windows](https://git-scm.com/) since it provides a Unix-like command line
environment useful for running shell scripts. Use of the bash shell under
Windows is mandatory for running the unit tests.

### Install OpenSSL

[Download the latest version of
OpenSSL.](http://slproweb.com/products/Win32OpenSSL.html) There will
several `Win64` bit variants available, you want the non-light
`v1.1` line. As of this writing, you **should** select

* Win64 OpenSSL v1.1.1L

and should **not** select

* Anything with "Win32" in the name
* Anything with "light" in the name
* Anything with "EXPERIMENTAL" in the name
* Anything in the 3.0 line - rippled won't currently build with this version.

Run the installer, and choose an appropriate location for your OpenSSL
installation. In this guide we use `C:\lib\OpenSSL-Win64` as the destination
location.

You may be informed on running the installer that "Visual C++ 2008
Redistributables" must first be installed first. If so, download it from the
[same page](http://slproweb.com/products/Win32OpenSSL.html), again making sure
to get the correct 32-/64-bit variant.

* NOTE: Since rippled links statically to OpenSSL, it does not matter where the
  OpenSSL .DLL files are placed, or what version they are. rippled does not use
  or require any external .DLL files to run other than the standard operating
  system ones.

### Build Boost

Boost 1.70 or later is required.

After [downloading boost](http://www.boost.org/users/download/) and unpacking it
to `c:\lib`. As of this writing, the most recent version of boost is 1.70.0,
which will unpack into a directory named `boost_1_70_0`. We recommended either
renaming this directory to `boost`, or creating a junction link `mklink /J boost
boost_1_70_0`, so that you can more easily switch between versions.

Next, open **Developer Command Prompt** and type the following commands

```powershell
cd C:\lib\boost
bootstrap
```

The rippled application is linked statically to the standard runtimes and
external dependencies on Windows, to ensure that the behavior of the executable
is not affected by changes in outside files. Therefore, it is necessary to build
the required boost static libraries using this command:

```powershell
bjam -j<Num Parallel> --toolset=msvc-14.1 address-model=64 architecture=x86 link=static threading=multi runtime-link=shared,static stage
```

where you should replace `<Num Parallel>` with the number of parallel
invocations to use build, e.g. `bjam -j4 ...` would use up to 4 concurrent build
shell commands for the build.

Building the boost libraries may take considerable time. When the build process
is completed, take note of both the reported compiler include paths and linker
library paths as they will be required later.

### (Optional) Install CMake for Windows

[CMake](http://cmake.org) is a cross platform build system generator. Visual
Studio 2017 includes an integrated version of CMake that avoids having to
manually run CMake, but it is undergoing continuous improvement. Users that
prefer to use standard Visual Studio project and solution files need to install
a dedicated version of CMake to generate them.  The latest version can be found
at the [CMake download site](https://cmake.org/download/). It is recommended you
select the install option to add CMake to your path.

## Clone the rippled repository

If you are familiar with cloning github repositories, just follow your normal
process and clone `git@github.com:ripple/rippled.git`. Otherwise follow this
section for instructions.

1. If you don't have a github account, sign up for one at
   [github.com](https://github.com/).
2. Make sure you have Github ssh keys. For help see
   [generating-ssh-keys](https://help.github.com/articles/generating-ssh-keys).

Open the "Git Bash" shell that was installed with "Git for Windows" in the step
above. Navigate to the directory where you want to clone rippled (git bash uses
`/c` for windows's `C:` and forward slash where windows uses backslash, so
`C:\Users\joe\projs` would be `/c/Users/joe/projs` in git bash). Now clone the
repository and optionally switch to the *master* branch. Type the following at
the bash prompt:

```powershell
git clone git@github.com:ripple/rippled.git
cd rippled
```
If you receive an error about not having the "correct access rights" make sure
you have Github ssh keys, as described above.

For a stable release, choose the `master` branch or one of the tagged releases
listed on [rippled's GitHub page](https://github.com/ripple/rippled/releases).

```
git checkout master
```

To test the latest release candidate, choose the `release` branch.

```
git checkout release
```

If you are doing development work and want the latest set of untested features,
you can consider using the `develop` branch instead.

```
git checkout develop
```

# Build using Visual Studio integrated CMake

In Visual Studio 2017, Microsoft added [integrated IDE support for
cmake](https://blogs.msdn.microsoft.com/vcblog/2016/10/05/cmake-support-in-visual-studio/).
To begin, simply:

1. Launch Visual Studio and choose **File | Open | Folder**, navigating to the
   cloned rippled folder.
2. Right-click on `CMakeLists.txt` in the **Solution Explorer - Folder View** to
   generate a `CMakeSettings.json` file. A sample settings file is provided
   [here](/Builds/VisualStudio2017/CMakeSettings-example.json). Customize the
   settings for `BOOST_ROOT`, `OPENSSL_ROOT` to match the install paths if they
   differ from those in the file.
4. Select either the `x64-Release` or `x64-Debug` configuration from the
   **Project Setings** drop-down. This should invoke the built-in CMake project
   generator. If not, you can right-click on the `CMakeLists.txt` file and
   choose **Cache | Generate Cache**.
5. Select either the `rippled.exe` (unity) or `rippled_classic.exe` (non-unity)
   option in the **Select Startup Item** drop-down. This will be the target
   built when you press F7. Alternatively, you can choose a target to build from
   the top-level **CMake | Build** menu. Note that at this time, there are other
   targets listed that come from third party visual studio files embedded in the
   rippled repo, e.g. `datagen.vcxproj`. Please ignore them.

For details on configuring debugging sessions or further customization of CMake,
please refer to the [CMake tools for VS
documentation](https://docs.microsoft.com/en-us/cpp/ide/cmake-tools-for-visual-cpp).

If using the provided `CMakeSettings.json` file, the executable will be in
```
.\build\x64-Release\Release\rippled.exe
```
or
```
.\build\x64-Debug\Debug\rippled.exe
```
These paths are relative to your cloned git repository.

# Build using stand-alone CMake

This requires having installed [CMake for
Windows](README.md#optional-install-cmake-for-windows). We do not recommend
mixing this method with the integrated CMake method for the same repository
clone. Assuming you included the cmake executable folder in your path,
execute the following commands within your `rippled` cloned repository:

```
mkdir build\cmake
cd build\cmake
cmake ..\.. -G"Visual Studio 15 2017 Win64" -DBOOST_ROOT="C:\lib\boost_1_70_0" -DOPENSSL_ROOT="C:\lib\OpenSSL-Win64" -DCMAKE_GENERATOR_TOOLSET=host=x64
```
Now launch Visual Studio 2017 and select **File | Open | Project/Solution**.
Navigate to the `build\cmake` folder created above and select the `rippled.sln`
file. You can then choose whether to build the `Debug` or `Release` solution
configuration.

The executable will be in
```
.\build\cmake\Release\rippled.exe
```
or
```
.\build\cmake\Debug\rippled.exe
```
These paths are relative to your cloned git repository.

# Unity/No-Unity Builds

The rippled build system defaults to using
[unity source files](http://onqtam.com/programming/2018-07-07-unity-builds/)
to improve build times. In some cases it might be desirable to disable the
unity build and compile individual translation units. Here is how you can
switch to a "no-unity" build configuration:

## Visual Studio Integrated CMake

Edit your `CmakeSettings.json` (described above) by adding `-Dunity=OFF`
to the `cmakeCommandArgs` entry for each build configuration.

## Standalone CMake Builds

When running cmake to generate the Visual Studio project files, add
`-Dunity=OFF` to the command line options passed to cmake.

**Note:** you will need to re-run the cmake configuration step anytime you
want to switch between unity/no-unity builds.

# Unit Test (Recommended)

`rippled` builds a set of unit tests into the server executable. To run these
unit tests after building, pass the `--unittest` option to the compiled
`rippled` executable. The executable will exit with summary info after running
the unit tests.

