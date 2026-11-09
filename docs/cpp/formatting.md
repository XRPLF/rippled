# C++ Formatting

All code must conform to `clang-format`, according to the settings in
[`.clang-format`](../../.clang-format), unless the result would be unreasonably
difficult to read or maintain. The `clang-format` version is pinned in
[`.pre-commit-config.yaml`](../../.pre-commit-config.yaml), so the
[`pre-commit`](../../CONTRIBUTING.md#pre-commit-hooks) hook always formats with the same version as
CI. To demarcate lines that should be left as-is, surround them with comments
like this:

```
// clang-format off
...
// clang-format on
```

Use this rarely, only where `clang-format`'s output is clearly harder to read
than a manual layout (e.g. aligned tables of values).

The easiest way to format your changes is to let the `pre-commit` hook run
automatically on commit, or to run it manually on all files or on specific ones:

```bash
pre-commit run clang-format --all-files
pre-commit run clang-format --files src/libxrpl/foo.cpp include/xrpl/foo.h
```

[`prek`](https://github.com/j178/prek) can be used as a drop-in replacement for
`pre-commit` in these commands.

CI runs all `pre-commit` hooks on pull requests. If a file does not conform, the
job fails and prints the required diff in its log; run the hooks locally as
above to fix it.
