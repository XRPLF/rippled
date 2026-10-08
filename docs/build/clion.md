# Building xrpld with CLion IDE

This guide covers opening the xrpld Conan/CMake project in [CLion](https://www.jetbrains.com/clion/).

Build xrpld from the command line first, following [BUILD.md](../../BUILD.md), to ensure there are no dependency issues. Doing so leaves you with a Conan build directory (for example, `.build`) that the steps below reuse.

If this is your first time opening the project in CLion, skip ahead to [Configure CLion](#configure-clion). The cache-reset step below is only needed to clear out a broken previous attempt.

## Reset the cache of CLion

Go to the root of the rippled repository and run the following commands to remove traces of previous
compilation runs:

1. `rm -rf .idea`
2. `rm -rf cmake-build-*`
3. `rm -f CMakeUserPresets.json` (if present — Conan's `CMakeToolchain` generator can create this file
   at the repository root)
4. CLion Toolbar -> File -> Invalidate Caches (click both options)
5. Remove the project from the CLion Quickstart menu

## Configure CLion

CLion can be configured to build and run xrpld by updating the [CMake
settings](https://www.jetbrains.com/help/clion/creating-new-project-from-scratch.html#open-prj), or it can import a [Compilation Database](https://www.jetbrains.com/help/clion/compilation-database.html) for code insight only. CLion
doesn't currently support building or running a project through a compilation database. Use only one of the two; don't set up both.

### Configuring CMake settings

1. Make sure the "Build type" field matches the one you passed to the `conan install` command; see
   [Build and Test](../../BUILD.md#build-and-test).
2. Set the "Build directory" field to the same directory you ran `conan install` in (for example, `.build`). The
   `-DCMAKE_TOOLCHAIN_FILE` path below is resolved relative to this directory, not the repository root.
3. Pass `-DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -Dxrpld=ON -Dtests=ON` in the
   "CMake options" field.
4. The `Toolchain` and `Generator` fields can be left at their defaults.

If CMake is configured correctly, you should see a toolbar with green icons for build and run.

### Using a Compilation Database

From your Conan build directory, generate a `compile_commands.json` file. If you installed with `build_type=Debug`, replace `Release` with `Debug`:

```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -Dxrpld=ON -Dtests=ON ..
```

This only works with `Makefile` or `Ninja` generators. [Other generators](https://cmake.org/cmake/help/latest/variable/CMAKE_EXPORT_COMPILE_COMMANDS.html), like `Visual Studio`, ignore it. See [Build and Test](../../BUILD.md#build-and-test) for more information on multi-configuration generators.

Once you have a `compile_commands.json` file, open the CLion project with it. After import, go to `Tools -> Compilation Database -> Change Project Root`. This gives CLion code insight (navigation, completion, analysis) for the project; continue building and running xrpld from the command line as usual.
