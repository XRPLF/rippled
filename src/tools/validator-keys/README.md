# validator-keys

Command-line tool for the keys behind an XRP Ledger validator or a validator-list
publisher: the key file, the manifest that delegates from the master key to a
signing key, the `[validator_token]` for `xrpld.cfg`, key revocation, domain
attestation, and signing and verifying validator lists.

It is built as a separate target of this repository and ships in the `xrpld`
packages as `/usr/bin/validator-keys`.

## Build

Configure with `-Dvalidator_keys=ON` and build the `validator-keys` target. With
`-Dtests=ON` the gtest suites under `src/tests/tools/validator-keys` build as
`validator_keys_tests`:

```
cmake --build . --target validator-keys validator_keys_tests
./validator_keys_tests
```

## Guide

[Validator Keys Tool Guide](doc/validator-keys-tool-guide.md)
