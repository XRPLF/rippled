# Conan lockfile

To achieve reproducible dependencies, we use a [Conan lockfile](https://docs.conan.io/2/tutorial/versioning/lockfiles.html).

The `conan.lock` file in the repository contains a "snapshot" of the current
dependencies. It is implicitly used when running `conan` commands, so you don't
need to specify it.

You have to update this file every time you add a new dependency or change a
revision or version of an existing dependency.

## Updating the lockfile

To update a lockfile, run the following commands from the repository root:

```bash
# Ensure that the xrplf remote is the first to be consulted, so any recipes we
# patched are used. We also add it there to not created huge diff when the
# official Conan Center Index is updated.
conan remote add --force --index 0 xrplf https://conan.ripplex.io

# Remove all local packages to prevent the local cache from influencing the
# lockfile.
conan remove '*' --confirm

# Create a new lockfile that is compatible with Linux, macOS, and Windows. The
# first create command will create a new lockfile, while the subsequent create
# commands will merge any additional dependencies into the created lockfile.
rm conan.lock
conan lock create . \
  --options '&:jemalloc=True' \
  --options '&:rocksdb=True' \
  --profile:all=conan/lockfile/linux.profile
conan lock create . \
  --options '&:jemalloc=True' \
  --options '&:rocksdb=True' \
  --profile:all=conan/lockfile/macos.profile
conan lock create . \
  --options '&:jemalloc=True' \
  --options '&:rocksdb=True' \
  --profile:all=conan/lockfile/windows.profile
```
