## From source

From a source build, you can install rippled and libxrpl using CMake's
`--install` mode:

```
cmake --install . --prefix /opt/local
```

The default [prefix][1] is typically `/usr/local` on Linux and macOS and
`C:/Program Files/rippled` on Windows.

[1]: https://cmake.org/cmake/help/latest/variable/CMAKE_INSTALL_PREFIX.html
