# Conan lockfile

To achieve reproducible dependencies, we use a [Conan lockfile](https://docs.conan.io/2/tutorial/versioning/lockfiles.html).

The `conan.lock` file in the repository contains a "snapshot" of the current
dependencies. It is implicitly used when running `conan` commands, so you don't
need to specify it.

You have to update this file every time you add a new dependency or change a
revision or version of an existing dependency.

To update a lockfile, run from the repository root: `./conan/lockfile/regenerate.sh`
