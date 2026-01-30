# API Version 2

API version 2 is available in `rippled` version 2.0.0 and later. To use this API, clients specify `"api_version" : 2` in each request.

For info about how [API versioning](https://xrpl.org/request-formatting.html#api-versioning) works, including examples, please view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155). API versioning ensures existing integrations and users continue to receive existing behavior, while those that request a higher API version will experience new behavior.

## Removed methods

In API version 2, the following deprecated methods are no longer available: ([#4759](https://github.com/XRPLF/rippled/pull/4759))

- `tx_history` - Instead, use other methods such as `account_tx` or `ledger` with the `transactions` field set to `true`.
- `ledger_header` - Instead, use the `ledger` method.

## Modifications to JSON transaction element in API version 2

In API version 2, JSON elements for transaction output have been changed and made consistent for all methods which output transactions. ([#4775](https://github.com/XRPLF/rippled/pull/4775))
This helps to unify the JSON serialization format of transactions. ([clio#722](https://github.com/XRPLF/clio/issues/722), [#4727](https://github.com/XRPLF/rippled/issues/4727))

- JSON transaction element is named `tx_json`
- Binary transaction element is named `tx_blob`
- JSON transaction metadata element is named `meta`
- Binary transaction metadata element is named `meta_blob`

Additionally, these elements are now consistently available next to `tx_json` (i.e. sibling elements), where possible:

- `hash` - Transaction ID. This data was stored inside transaction output in API version 1, but in API version 2 is a sibling element.
- `ledger_index` - Ledger index (only set on validated ledgers)
- `ledger_hash` - Ledger hash (only set on closed or validated ledgers)
- `close_time_iso` - Ledger close time expressed in ISO 8601 time format (only set on validated ledgers)
- `validated` - Bool element set to `true` if the transaction is in a validated ledger, otherwise `false`

This change affects the following methods:

- `tx` - Transaction data moved into element `tx_json` (was inline inside `result`) or, if binary output was requested, moved from `tx` to `tx_blob`. Renamed binary transaction metadata element (if it was requested) from `meta` to `meta_blob`. Changed location of `hash` and added new elements
- `account_tx` - Renamed transaction element from `tx` to `tx_json`. Renamed binary transaction metadata element (if it was requested) from `meta` to `meta_blob`. Changed location of `hash` and added new elements
- `transaction_entry` - Renamed transaction metadata element from `metadata` to `meta`. Changed location of `hash` and added new elements
- `subscribe` - Renamed transaction element from `transaction` to `tx_json`. Changed location of `hash` and added new elements
- `sign`, `sign_for`, `submit` and `submit_multisigned` - Changed location of `hash` element.

## Modifications to `Payment` transaction JSON schema

When reading Payments, the `Amount` field should generally **not** be used. Instead, use [delivered_amount](https://xrpl.org/partial-payments.html#the-delivered_amount-field) to see the amount that the Payment delivered. To clarify its meaning, the `Amount` field is being renamed to `DeliverMax`. ([#4733](https://github.com/XRPLF/rippled/pull/4733))

- In `Payment` transaction type, JSON RPC field `Amount` is renamed to `DeliverMax`. To enable smooth client transition, `Amount` is still handled, as described below: ([#4733](https://github.com/XRPLF/rippled/pull/4733))
  - On JSON RPC input (e.g. `submit_multisigned` etc. methods), `Amount` is recognized as an alias to `DeliverMax` for both API version 1 and version 2 clients.
  - On JSON RPC input, submitting both `Amount` and `DeliverMax` fields is allowed _only_ if they are identical; otherwise such input is rejected with `rpcINVALID_PARAMS` error.
  - On JSON RPC output (e.g. `subscribe`, `account_tx` etc. methods), `DeliverMax` is present in both API version 1 and version 2.
  - On JSON RPC output, `Amount` is only present in API version 1 and _not_ in version 2.

## Modifications to account_info response

- `signer_lists` is returned in the root of the response. (In API version 1, it was nested under `account_data`.) ([#3770](https://github.com/XRPLF/rippled/pull/3770))
- When using an invalid `signer_lists` value, the API now returns an "invalidParams" error. ([#4585](https://github.com/XRPLF/rippled/pull/4585))
  - (`signer_lists` must be a boolean. In API version 1, strings were accepted and may return a normal response - i.e. as if `signer_lists` were `true`.)

## Modifications to [account_tx](https://xrpl.org/account_tx.html#account_tx) response

- Using `ledger_index_min`, `ledger_index_max`, and `ledger_index` returns `invalidParams` because if you use `ledger_index_min` or `ledger_index_max`, then it does not make sense to also specify `ledger_index`. In API version 1, no error was returned. ([#4571](https://github.com/XRPLF/rippled/pull/4571))
  - The same applies for `ledger_index_min`, `ledger_index_max`, and `ledger_hash`. ([#4545](https://github.com/XRPLF/rippled/issues/4545#issuecomment-1565065579))
- Using a `ledger_index_min` or `ledger_index_max` beyond the range of ledgers that the server has:
  - returns `lgrIdxMalformed` in API version 2. Previously, in API version 1, no error was returned. ([#4288](https://github.com/XRPLF/rippled/issues/4288))
- Attempting to use a non-boolean value (such as a string) for the `binary` or `forward` parameters returns `invalidParams` (`rpcINVALID_PARAMS`). Previously, in API version 1, no error was returned. ([#4620](https://github.com/XRPLF/rippled/pull/4620))

## Modifications to [noripple_check](https://xrpl.org/noripple_check.html#noripple_check) response

- Attempting to use a non-boolean value (such as a string) for the `transactions` parameter returns `invalidParams` (`rpcINVALID_PARAMS`). Previously, in API version 1, no error was returned. ([#4620](https://github.com/XRPLF/rippled/pull/4620))
