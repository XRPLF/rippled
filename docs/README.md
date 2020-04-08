# Building documentation

## Dependencies

Install these dependencies:

- [Doxygen](http://www.doxygen.nl): All major platforms have [official binary
  distributions](http://www.doxygen.nl/download.html#srcbin), or you can
  build from [source](http://www.doxygen.nl/download.html#srcbin).

  - MacOS: We recommend installing via Homebrew: `brew install doxygen`.
    The executable will be installed in `/usr/local/bin` which is already
    in the default `PATH`.

    If you use the official binary distribution, then you'll need to make
    Doxygen available to your command line. You can do this by adding
    a symbolic link from `/usr/local/bin` to the `doxygen` executable. For
    example,

    ```
    $ ln -s /Applications/Doxygen.app/Contents/Resources/doxygen /usr/local/bin/doxygen
    ```

- [PlantUML](http://plantuml.com): 

  1. Install a functioning Java runtime, if you don't already have one.
  2. Download [`plantuml.jar`](http://sourceforge.net/projects/plantuml/files/plantuml.jar/download).

- [Graphviz](https://www.graphviz.org):

  - Linux: Install from your package manager.
  - Windows: Use an [official installer](https://graphviz.gitlab.io/_pages/Download/Download_windows.html).
  - MacOS: Install via Homebrew: `brew install graphviz`.


## Docker

Instead of installing the above dependencies locally, you can use the official
build environment Docker image, which has all of them installed already.

1. Install [Docker](https://docs.docker.com/engine/installation/)
2. Pull the image:
  ```
  sudo docker pull rippleci/rippled-ci-builder:2944b78d22db
  ```
3. Run the image from the project folder:
  ```
  sudo docker run -v $PWD:/opt/rippled --rm rippleci/rippled-ci-builder:2944b78d22db
  ```


## Build

There is a `docs` target in the CMake configuration.

```
mkdir build
cd build
cmake ..
cmake --build . --target docs
```

The output will be in `build/docs/html`.
