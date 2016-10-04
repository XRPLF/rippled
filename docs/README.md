# Building documentation

## Install Dependencies

### Windows

Install these dependencies:

1. Install [Doxygen](http://www.stack.nl/~dimitri/doxygen/download.html)
2. Download the following zip files from [xsltproc](https://www.zlatkovic.com/pub/libxml/)
  (Alternate download: ftp://ftp.zlatkovic.com/libxml/),
  and extract the `bin\` folder contents into any folder in your path.
  * iconv
  * libxml2
  * libxslt
  * zlib
3. Download [Boost](http://www.boost.org/users/download/)
  1. Extract the compressed file contents to your (new) `$BOOST_ROOT` location.
  2. Open a command prompt or shell in the `$BOOST_ROOT`.
  3. `./bootstrap.bat`
  4. (Optional, if you also plan to build rippled) `./bjam.exe --toolset=msvc-14.0
   --build-type=complete variant=debug,release link=static runtime-link=static
   address-model=64 stage`
  5. If it is not already there, add your `$BOOST_ROOT` to your environment `$PATH`.

### MacOS

Under construction

### Linux

Under construction

## Setup project submodules

1. Open a shell in your rippled root folder.
2. `git submodule init`
3. `git submodule update docs/docca`

## Do it

From the rippled root folder:
```
cd docs
./makeqbk.sh && b2
```
The output will be in `docs/html`.
