# Unit tests

This directory contains unit tests for the project. The difference from existing `src/test` folder
is that we switch to 3rd party testing framework (`gtest`). We intend to gradually move existing tests
from our own framework to `gtest` and such tests will be moved to this new folder.

## Layout

`libxrpl/` has one directory per libxrpl module, and two that are not modules: `helpers/` for test
infrastructure shared by all of them, and `csf/` for the consensus simulation framework. Each
directory is compiled into its own object library, and all of them are linked into the single
`xrpl_tests` binary. `xrpld/` has one directory per server subsystem, all compiled into the
`xrpld_tests` binary.

A test includes a header from the test tree under its canonical path, `<tests/libxrpl/<dir>/...>` or
`<tests/xrpld/<dir>/...>`, the same way libxrpl's private headers are included. Under `libxrpl/` the
headers of a directory are only visible to that directory and to the directories that link its
target, so a test that needs another directory's headers has to declare that dependency in
`libxrpl/CMakeLists.txt`. The `helpers/` directory is the exception: every directory can include it
without declaring anything. Under `xrpld/` the directories cannot be isolated the same way, because
the server headers are not a library and the whole `src` tree is on the include path, so there the
canonical path is a convention only.
