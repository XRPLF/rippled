We recommend two different methods to depend on libxrpl in your own [CMake][]
project.
Both methods add a CMake library target named `xrpl::libxrpl`.


## Conan requirement 

The first method adds libxrpl as a [Conan][] requirement.
With this method, there is no need for a Git [submodule][].
It is good for when you just need a dependency on libxrpl as-is.

```
# This conanfile.txt is just an example.
[requires]
xrpl/1.10.0

[generators]
CMakeDeps
CMakeToolchain
```

```
# If you want to depend on a version of libxrpl that is not in ConanCenter,
# then you can export the recipe from the rippled project.
conan export <path>
```

```cmake
# Find and link the library in your CMake project.
find_package(xrpl)
target_link_libraries(<target> PUBLIC xrpl::libxrpl)
```

```
# Download, build, and connect dependencies with Conan.
mkdir .build
cd .build
mkdir -p build/generators
conan install \
  --install-folder build/generators \
  --build missing \
  --settings build_type=Release \
  ..
cmake \
  -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  ..
cmake --build . --parallel
```


## CMake subdirectory

The second method adds the [rippled][] project as a CMake
[subdirectory][add_subdirectory].
This method works well when you keep the rippled project as a Git
[submodule][].
It's good for when you want to make changes to libxrpl as part of your own
project.
Be careful, though.
Your project will inherit all of the same CMake options,
so watch out for name collisions.
We still recommend using [Conan][] to download, build, and connect dependencies.

```
# Add the project as a Git submodule.
mkdir submodules
git submodule add https://github.com/XRPLF/rippled.git submodules/rippled
```

```cmake
# Add and link the library in your CMake project.
add_subdirectory(submodules/rippled)
target_link_libraries(<target> PUBLIC xrpl::libxrpl)
```

```
# Download, build, and connect dependencies with Conan.
mkdir .build
cd .build
conan install \
  --output-folder . \
  --build missing \
  --settings build_type=Release \
  ../submodules/rippled
cmake \
  -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  ..
cmake --build . --parallel
```


[add_subdirectory]: https://cmake.org/cmake/help/latest/command/add_subdirectory.html
[submodule]: https://git-scm.com/book/en/v2/Git-Tools-Submodules
[rippled]: https://github.com/ripple/rippled
[Conan]: https://docs.conan.io/
[CMake]: https://cmake.org/cmake/help/latest/
