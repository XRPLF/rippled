# Advanced Conan configuration

This document provides advanced instructions for setting up and configuring Conan for `xrpld` development: custom profiles, the lockfile, patched recipes, and profile tweaks.

## Custom profile

If the default profile does not work for you and you do not yet have a Conan
profile, you can create one by running:

```bash
conan profile detect
```

You may need to make changes to the profile to suit your environment. You can
refer to the provided `conan/profiles/default` profile for inspiration, and you
may also need to apply the required [tweaks](#conan-profile-tweaks) to this
default profile.

## Conan lockfile

To achieve reproducible dependencies, we use a [Conan lockfile](https://docs.conan.io/2/tutorial/versioning/lockfiles.html),
which has to be updated every time dependencies change.

Please see the [instructions on how to regenerate the lockfile](../../conan/lockfile/README.md).

## Patched recipes

Occasionally, we need patched recipes or recipes not present in Conan Center.
We maintain a fork of the Conan Center Index
[here](https://github.com/XRPLF/conan-center-index/) containing the modified and newly added recipes.

To ensure our patched recipes are used, you must add our Conan remote at a
higher index than the default Conan Center remote, so it is consulted first. You
can do this by running:

```bash
conan remote add --index 0 --force xrplf https://conan.ripplex.io
```

Alternatively, you can pull our recipes from the repository and export them locally:

```bash
# Define which recipes to export.
recipes=('abseil' 'ed25519' 'mpt-crypto' 'openssl' 'secp256k1' 'snappy' 'soci' 'wasm-xrplf' 'wasmi')

# Selectively check out the recipes from our CCI fork.
cd external
mkdir -p conan-center-index
cd conan-center-index
git init
git remote add origin git@github.com:XRPLF/conan-center-index.git
git sparse-checkout init
for recipe in "${recipes[@]}"; do
    echo "Checking out recipe '${recipe}'..."
    git sparse-checkout add recipes/${recipe}
done
git fetch origin master
git checkout master

./export_all.sh
cd ../../
```

In the case we switch to a newer version of a dependency that still requires a
patch or add a new dependency, it will be necessary for you to pull in the changes and re-export the
updated dependencies with the newer version. However, if we switch to a newer
version that no longer requires a patch, no action is required on your part, as
the new recipe will be automatically pulled from the official Conan Center.

> [!NOTE]
> You might need to add `--lockfile=""` to your `conan install` command
> to avoid automatic use of the existing `conan.lock` file when you run
> `conan export` manually on your machine
>
> This is not recommended though, as you might end up using different revisions of recipes.

## Conan profile tweaks

### Missing compiler version

If you see an error similar to the following after running `conan profile show`:

```text
ERROR: Invalid setting '17' is not a valid 'settings.compiler.version' value.
Possible values are ['5.0', '5.1', '6.0', '6.1', '7.0', '7.3', '8.0', '8.1',
'9.0', '9.1', '10.0', '11.0', '12.0', '13', '13.0', '13.1', '14', '14.0', '15',
'15.0', '16', '16.0']
Read "http://docs.conan.io/2/knowledge/faq.html#error-invalid-setting"
```

you need to create `$(conan config home)/settings_user.yml` file if it doesn't exist and add the required version number(s)
to the `version` array specific for your compiler. For example:

```yaml
compiler:
  apple-clang:
    version: ["17.0"]
```

### Multiple compilers

If you have multiple compilers installed, make sure to select the one to use in
your default Conan configuration **before** running `conan profile detect`, by
setting the `CC` and `CXX` environment variables.

For example, if you are running MacOS and have [homebrew
LLVM@18](https://formulae.brew.sh/formula/llvm@18), and want to use it as a
compiler in the new Conan profile:

```bash
export CC=$(brew --prefix llvm@18)/bin/clang
export CXX=$(brew --prefix llvm@18)/bin/clang++
conan profile detect
```

You should also explicitly set the path to the compiler in the profile file,
which helps to avoid errors when `CC` and/or `CXX` are set and disagree with the
selected Conan profile. For example:

```text
[conf]
tools.build:compiler_executables={'c':'/usr/bin/gcc','cpp':'/usr/bin/g++'}
```

### Multiple profiles

You can manage multiple Conan profiles in the directory
`$(conan config home)/profiles`, for example renaming `default` to a different
name and then creating a new `default` profile for a different compiler.

### Select language

The default profile created by Conan will typically select different C++ dialect
than C++23 used by this project. You should set `23` in the profile line
starting with `compiler.cppstd=`. For example:

```bash
sed -i.bak -e 's|^compiler\.cppstd=.*$|compiler.cppstd=23|' $(conan config home)/profiles/default
```

### Select standard library in Linux

**Linux** developers will commonly have a default Conan [profile][] that
compiles with GCC and links with libstdc++. If you are linking with libstdc++
(see profile setting `compiler.libcxx`), then you will need to choose the
`libstdc++11` ABI:

```bash
sed -i.bak -e 's|^compiler\.libcxx=.*$|compiler.libcxx=libstdc++11|' $(conan config home)/profiles/default
```

### Select architecture and runtime in Windows

**Windows** developers may need to use the x64 native build tools. An easy way
to do that is to run the shortcut "x64 Native Tools Command Prompt" for the
version of Visual Studio that you have installed.

Windows developers must also build `xrpld` and its dependencies for the x64
architecture:

```bash
sed -i.bak -e 's|^arch=.*$|arch=x86_64|' $(conan config home)/profiles/default
```

**Windows** developers also must select static runtime:

```bash
sed -i.bak -e 's|^compiler\.runtime=.*$|compiler.runtime=static|' $(conan config home)/profiles/default
```

## Add a Dependency

If you want to experiment with a new package, follow these steps:

1. Search for the package on [Conan Center](https://conan.io/center/).
2. Modify [`conanfile.py`](../../conanfile.py):
   - Add a version of the package to the `requires` property.
   - Change any default options for the package by adding them to the
     `default_options` property (with syntax `'$package:$option': $value`).
3. Regenerate the [Conan lockfile](../../conan/lockfile/README.md) so the new
   dependency is captured:

   ```bash
   ./conan/lockfile/regenerate.sh
   ```

4. Modify [`CMakeLists.txt`](../../CMakeLists.txt):
   - Add a call to `find_package($package REQUIRED)`.
   - Link a library from the package to the target `xrpl_libs`
     (search for the existing call to `target_link_libraries(xrpl_libs INTERFACE ...)`).
5. Start coding! Don't forget to include whatever headers you need from the package.

[profile]: https://docs.conan.io/2/reference/config_files/profiles.html
