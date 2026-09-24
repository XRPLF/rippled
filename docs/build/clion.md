# Building xrpld with CLion IDE

This guide covers opening the xrpld Conan/CMake project in [CLion](https://www.jetbrains.com/clion/).

Build xrpld from the command line first, following [BUILD.md](../../BUILD.md), to ensure there are no dependency issues.

If this is your first time opening the project in CLion, skip ahead to [Configure CLion](#configure-clion). The cache-reset step below is only needed to clear out a broken previous attempt.

## Reset the cache of CLion

Go to the root of the rippled repository and run the following commands to remove traces of previous
compilation runs:

1. `rm -rf .idea`
2. `rm -rf cmake-build-*`
3. `rm -rf cmake-release-*`
4. `rm -f CMakeUserPresets.json` (if present — Conan's `CMakeToolchain` generator can create this file
   at the repository root)
5. CLion Toolbar -> File -> Invalidate Caches (click both options)
6. Remove the project from the CLion Quickstart menu

## Configure CLion

CLion can be configured to run xrpld by updating the [CMake
settings](https://www.jetbrains.com/help/clion/creating-new-project-from-scratch.html#open-prj) or by
enabling a [Compilation
Database](https://www.jetbrains.com/help/clion/compilation-database.html). Use only one of the two —
don't set up both.

### Configuring CMake settings

1. Make sure the "Build type" field matches the one you passed to the `conan install` command; see
   [Build and Test](../../BUILD.md#build-and-test).
2. Make sure you pass `-DCMAKE_TOOLCHAIN_FILE` in the "CMake options" field, as described in the same
   section.
3. The `Toolchain` and `Generator` fields can be left at their defaults.

### Using a Compilation Database

You can use the following command to generate a `compile_commands.json` file:

```bash
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
```

Then open the CLion project with the `compile_commands.json` file. After import, go to `Tools ->
Compilation Database -> Change Project Root`.

If CMake is configured correctly, you should see a toolbar with green icons for build and run.
