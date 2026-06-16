# Release Notes

# Change Log

# Releases

## Version 0.3.2

This release overhauls the Travis CI configuration to cover more cases more robustly, and fixes a Windows build error introduced in 0.3.1.

### New and Improved Features

- Restructure Travis CI builds to use rippled's infrastructure [[#16](https://github.com/ripple/validator-keys-tool/pull/16)].

### Bug Fixes

- Restores the windows.h include removed in 0.3.1, which is required for Windows builds.

## Version 0.3.1

This version brings the code up to date with the rippled code base's internal APIs and structures.

### Bug Fixes

- Update includes paths [[#14](https://github.com/ripple/validator-keys-tool/pull/14)].
