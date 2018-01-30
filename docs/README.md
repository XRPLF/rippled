# Building documentation

## Specifying Files

To specify the source files for which to build documentation, modify `INPUT`
and its related fields in `docs/source.dox`. Note that the `INPUT` paths are
relative to the `docs/` directory.

## Install Dependencies

### Windows

Install these dependencies:

1. Install [Doxygen](http://www.stack.nl/~dimitri/doxygen/download.html)
2. Download [plantuml](http://plantuml.com) from
   [here](http://sourceforge.net/projects/plantuml/files/plantuml.jar/download).
   Set a system environment variable named `PLANTUML_JAR` with a value of the fullpath
   the this file system location of the `plantuml.jar` file you downloaded.
3. Download/install a functioning java runtime, if you don't already have one.

### MacOS

1. Install doxygen:
  * Use homebrew to install: `brew install doxygen`.  The executable will be
    installed in `/usr/local/bin` which is already in your path.
  * Alternatively, install from here: [doxygen](http://www.stack.nl/~dimitri/doxygen/download.html).
    You'll then need to make doxygen available to your command line.  You can
    do this by adding a symbolic link from `/usr/local/bin` to the doxygen
    executable.  For example, `$ ln -s /Applications/Doxygen.app/Contents/Resources/doxygen /usr/local/bin/doxygen`
2. Download [plantuml](http://plantuml.com) from
   [here](http://sourceforge.net/projects/plantuml/files/plantuml.jar/download).
   Set a system environment variable named `PLANTUML_JAR` with a value of the fullpath
   the this file system location of the `plantuml.jar` file you downloaded.
3. Download/install a functioning java runtime, if you don't already have one.

### Linux

1. Install doxygen using your package manager OR from source using the links above.
2. Download [plantuml](http://plantuml.com) from
   [here](http://sourceforge.net/projects/plantuml/files/plantuml.jar/download).
   Set a system environment variable named `PLANTUML_JAR` with a value of the fullpath
   the this file system location of the `plantuml.jar` file you downloaded.
3. Download/install a functioning java runtime, if you don't already have one.

## Do it

### all platforms

From the rippled root folder:
```
cd docs
mkdir -p html_doc
doxygen source.dox
```
The output will be in `docs/html_doc`.

## Docker

(applicable to all platforms)
    
Instead of installing the doxygen tools locally, you can use the provided `Dockerfile` to create
an ubuntu based image for running the tools:

1. Install [Docker](https://docs.docker.com/engine/installation/)
2. Build Docker image. From the rippled root folder:

```
sudo docker build -t rippled-docs docs/
```

The to run the image, from the rippled root folder:

```
sudo docker run -v $PWD:/opt/rippled --rm rippled-docs
```

The output will be in `docs/html_doc`.

