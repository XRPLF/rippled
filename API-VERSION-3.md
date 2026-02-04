# API Version 3

API version 3 is currently a **beta API**. It requires enabling `[beta_rpc_api]` in the rippled configuration to use. To use this API, clients specify `"api_version" : 3` in each request.

For info about how [API versioning](https://xrpl.org/request-formatting.html#api-versioning) works, including examples, please view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155). API versioning ensures existing integrations and users continue to receive existing behavior, while those that request a higher API version will experience new behavior.

## Breaking Changes

### Modifications to `amm_info`

The order of error checks has been changed to provide more specific error messages. ([#4924](https://github.com/XRPLF/rippled/pull/4924))

- **Before (API v2)**: When sending an invalid account or asset to `amm_info` while other parameters are not set as expected, the method returns a generic `rpcINVALID_PARAMS` error.
- **After (API v3)**: The same scenario returns a more specific error: `rpcISSUE_MALFORMED` for malformed assets or `rpcACT_MALFORMED` for malformed accounts.

### Modifications to `ledger_entry`

Added support for string shortcuts to look up fixed-location ledger entries using the `"index"` parameter. ([#5644](https://github.com/XRPLF/rippled/pull/5644))

In API version 3, the following string values can be used with the `"index"` parameter:

- `"index": "amendments"` - Returns the `Amendments` ledger entry
- `"index": "fee"` - Returns the `FeeSettings` ledger entry
- `"index": "nunl"` - Returns the `NegativeUNL` ledger entry
- `"index": "hashes"` - Returns the "short" `LedgerHashes` ledger entry (recent ledger hashes)

These shortcuts are only available in API version 3 and later. In API versions 1 and 2, these string values would result in an error.
