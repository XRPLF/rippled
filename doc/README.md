# Building documentation

## Specifying Files

To specify the source files for which to build documentation, modify `INPUT`
and its related fields in `doc/source.dox`. Note that the `INPUT` paths are
relative to the `doc/` directory.

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
  4. If it is not already there, add your `$BOOST_ROOT` to your environment `$PATH`.

### MacOS

1. Install doxygen:
  * Use homebrew to install: `brew install doxygen`.  The executable will be
    installed in `/usr/local/bin` which is already in your path.
  * Alternatively, install from here: [doxygen](http://www.stack.nl/~dimitri/doxygen/download.html).
    You'll then need to make doxygen available to your command line.  You can
    do this by adding a symbolic link from `/usr/local/bin` to the doxygen
    executable.  For example, `$ ln -s /Applications/Doxygen.app/Contents/Resources/doxygen /usr/local/bin/doxygen`
2. Install [Boost](http://www.boost.org/users/download/)
  1. Extract the compressed file contents to your (new) `$BOOST_ROOT` location.
  2. Open a command prompt or shell in the `$BOOST_ROOT`.
  3. `$ ./bootstrap.bat`
  4. If it is not already there, add your `$BOOST_ROOT` to your environment
     `$PATH`.  This makes the `b2` command available to the command line.
3. That should be all that's required.  In OS X 10.11, at least, libxml2 and
   libxslt come pre-installed.

### Linux

1. Install [Docker](https://docs.docker.com/engine/installation/)
2. Build Docker image. From the Beast root folder:
```
sudo docker build -t beast-docs doc/
```

## Do it

### Windows & MacOS

From the Beast root folder:
```
cd doc
./makeqbk.sh && b2
```
The output will be in `doc/html`.

### Linux

From the Beast root folder:
```
sudo docker run -v $PWD:/opt/beast --rm beast-docs
```
The output will be in `doc/html`.
