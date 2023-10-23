# API Changelog

This changelog is intended to list all updates to the [public API methods](https://xrpl.org/public-api-methods.html).

For info about how [API versioning](https://xrpl.org/request-formatting.html#api-versioning) works, including examples, please view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155). API versioning ensures existing integrations and users continue to receive existing behavior, while those that request a higher API version will experience new behavior.

The API version controls the API behavior you see. This includes what properties you see in responses, what parameters you're permitted to send in requests, and so on. You specify the API version in each of your requests. When a breaking change is introduced to the `rippled` API, a new version is released. To avoid breaking your code, you should set (or increase) your version when you're ready to upgrade.

For a log of breaking changes, see the **API Version [number]** headings. In general, breaking changes are associated with a particular API Version number. For non-breaking changes, scroll to the **XRP Ledger version [x.y.z]** headings. Non-breaking changes are associated with a particular XRP Ledger (`rippled`) release.

## API Version 1

This version is supported by all `rippled` versions. At time of writing, it is the default API version, used when no `api_version` is specified. When a new API version is introduced, the command line interface will default to the latest API version. The command line is intended for ad-hoc usage by humans, not programs or automated scripts. The command line is not meant for use in production code.

### Idiosyncrasies

#### V1 account_info response

In [the response to the `account_info` command](https://xrpl.org/account_info.html#response-format), there is `account_data` - which is supposed to be an `AccountRoot` object - and `signer_lists` is returned in this object. However, the docs say that `signer_lists` should be at the root level of the reponse.

It makes sense for `signer_lists` to be at the root level because signer lists are not part of the AccountRoot object. (First reported in [xrpl-dev-portal#938](https://github.com/XRPLF/xrpl-dev-portal/issues/938).)

In `api_version: 2`, the `signer_lists` field [will be moved](#modifications-to-account_info-response-in-v2) to the root level of the account_info response. (https://github.com/XRPLF/rippled/pull/3770)

#### server_info - network_id

The `network_id` field was added in the `server_info` response in version 1.5.0 (2019), but it was not returned in [reporting mode](https://xrpl.org/rippled-server-modes.html#reporting-mode).

## Unreleased

### Additions

Additions are intended to be non-breaking (because they are purely additive).

- `server_definitions`: A new RPC that generates a `definitions.json`-like output that can be used in XRPL libraries.

## XRP Ledger version 1.12.0

[Version 1.12.0](https://github.com/XRPLF/rippled/releases/tag/1.12.0) was released on Sep 6, 2023.

### Additions in 1.12

Additions are intended to be non-breaking (because they are purely additive).

- `server_info`: Added `ports`, an array which advertises the RPC and WebSocket ports. This information is also included in the `/crawl` endpoint (which calls `server_info` internally). `grpc` and `peer` ports are also included. (https://github.com/XRPLF/rippled/pull/4427)
  - `ports` contains objects, each containing a `port` for the listening port (a number string), and a `protocol` array listing the supported protocols on that port.
  - This allows crawlers to build a more detailed topology without needing to port-scan nodes.
  - (For peers and other non-admin clients, the info about admin ports is excluded.)
- Clawback: The following additions are gated by the Clawback amendment (`featureClawback`). (https://github.com/XRPLF/rippled/pull/4553)
  - Adds an [AccountRoot flag](https://xrpl.org/accountroot.html#accountroot-flags) called `lsfAllowTrustLineClawback` (https://github.com/XRPLF/rippled/pull/4617)
    - Adds the corresponding `asfAllowTrustLineClawback` [AccountSet Flag](https://xrpl.org/accountset.html#accountset-flags) as well.
    - Clawback is disabled by default, so if an issuer desires the ability to claw back funds, they must use an `AccountSet` transaction to set the AllowTrustLineClawback flag. They must do this before creating any trust lines, offers, escrows, payment channels, or checks.
  - Adds the [Clawback transaction type](https://github.com/XRPLF/XRPL-Standards/blob/master/XLS-39d-clawback/README.md#331-clawback-transaction), containing these fields:
    - `Account`: The issuer of the asset being clawed back. Must also be the sender of the transaction.
    - `Amount`: The amount being clawed back, with the `Amount.issuer` being the token holder's address.
- Adds [AMM](https://github.com/XRPLF/XRPL-Standards/discussions/78) ([#4294](https://github.com/XRPLF/rippled/pull/4294), [#4626](https://github.com/XRPLF/rippled/pull/4626)) feature:
  - Adds `amm_info` API to retrieve AMM information for a given tokens pair.
  - Adds `AMMCreate` transaction type to create `AMM` instance.
  - Adds `AMMDeposit` transaction type to deposit funds into `AMM` instance.
  - Adds `AMMWithdraw` transaction type to withdraw funds from `AMM` instance.
  - Adds `AMMVote` transaction type to vote for the trading fee of `AMM` instance.
  - Adds `AMMBid` transaction type to bid for the Auction Slot of `AMM` instance.
  - Adds `AMMDelete` transaction type to delete `AMM` instance.
  - Adds `sfAMMID` to `AccountRoot` to indicate that the account is `AMM`'s account. `AMMID` is used to fetch `ltAMM`.
  - Adds `lsfAMMNode` `TrustLine` flag to indicate that one side of the `TrustLine` is `AMM` account.
  - Adds `tfLPToken`, `tfSingleAsset`, `tfTwoAsset`, `tfOneAssetLPToken`, `tfLimitLPToken`, `tfTwoAssetIfEmpty`,
    `tfWithdrawAll`, `tfOneAssetWithdrawAll` which allow a trader to specify different fields combination
    for `AMMDeposit` and `AMMWithdraw` transactions.
  - Adds new transaction result codes:
    - tecUNFUNDED_AMM: insufficient balance to fund AMM. The account does not have funds for liquidity provision.
    - tecAMM_BALANCE: AMM has invalid balance. Calculated balances greater than the current pool balances.
    - tecAMM_FAILED: AMM transaction failed. Fails due to a processing failure.
    - tecAMM_INVALID_TOKENS: AMM invalid LP tokens. Invalid input values, format, or calculated values.
    - tecAMM_EMPTY: AMM is in empty state. Transaction expects AMM in non-empty state (LP tokens > 0).
    - tecAMM_NOT_EMPTY: AMM is not in empty state. Transaction expects AMM in empty state (LP tokens == 0).
    - tecAMM_ACCOUNT: AMM account. Clawback of AMM account.
    - tecINCOMPLETE: Some work was completed, but more submissions required to finish. AMMDelete partially deletes the trustlines.

## XRP Ledger version 1.11.0

[Version 1.11.0](https://github.com/XRPLF/rippled/releases/tag/1.11.0) was released on Jun 20, 2023.

### Breaking changes in 1.11

- Added the ability to mark amendments as obsolete. For the `feature` admin API, there is a new possible value for the `vetoed` field. (https://github.com/XRPLF/rippled/pull/4291)
  - The value of `vetoed` can now be `true`, `false`, or `"Obsolete"`.
- Removed the acceptance of seeds or public keys in place of account addresses. (https://github.com/XRPLF/rippled/pull/4404)
  - This simplifies the API and encourages better security practices (i.e. seeds should never be sent over the network).
- For the `ledger_data` method, when all entries are filtered out, the `state` field of the response is now an empty list (in other words, an empty array, `[]`). (Previously, it would return `null`.) While this is technically a breaking change, the new behavior is consistent with the documentation, so this is considered only a bug fix. (https://github.com/XRPLF/rippled/pull/4398)
- If and when the `fixNFTokenRemint` amendment activates, there will be a new AccountRoot field, `FirstNFTSequence`. This field is set to the current account sequence when the account issues their first NFT. If an account has not issued any NFTs, then the field is not set. ([#4406](https://github.com/XRPLF/rippled/pull/4406))
  - There is a new account deletion restriction: an account can only be deleted if `FirstNFTSequence` + `MintedNFTokens` + `256` is less than the current ledger sequence.
  - This is potentially a breaking change if clients have logic for determining whether an account can be deleted.
- NetworkID
  - For sidechains and networks with a network ID greater than 1024, there is a new [transaction common field](https://xrpl.org/transaction-common-fields.html), `NetworkID`. (https://github.com/XRPLF/rippled/pull/4370)
    - This field helps to prevent replay attacks and is now required for chains whose network ID is 1025 or higher.
    - The field must be omitted for Mainnet, so there is no change for Mainnet users.
  - There are three new local error codes:
    - `telNETWORK_ID_MAKES_TX_NON_CANONICAL`: a `NetworkID` is present but the chain's network ID is less than 1025. Remove the field from the transaction, and try again.
    - `telREQUIRES_NETWORK_ID`: a `NetworkID` is required, but is not present. Add the field to the transaction, and try again.
    - `telWRONG_NETWORK`: a `NetworkID` is specified, but it is for a different network. Submit the transaction to a different server which is connected to the correct network.

### Additions and bug fixes in 1.11

- Added `nftoken_id`, `nftoken_ids` and `offer_id` meta fields into NFT `tx` and `account_tx` responses. (https://github.com/XRPLF/rippled/pull/4447)
- Added an `account_flags` object to the `account_info` method response. (https://github.com/XRPLF/rippled/pull/4459)
- Added `NFTokenPages` to the `account_objects` RPC. (https://github.com/XRPLF/rippled/pull/4352)
- Fixed: `marker` returned from the `account_lines` command would not work on subsequent commands. (https://github.com/XRPLF/rippled/pull/4361)

## XRP Ledger version 1.10.0

[Version 1.10.0](https://github.com/XRPLF/rippled/releases/tag/1.10.0)
was released on Mar 14, 2023.

### Breaking changes in 1.10

- If the `XRPFees` feature is enabled, the `fee_ref` field will be
  removed from the [ledger subscription stream](https://xrpl.org/subscribe.html#ledger-stream), because it will no longer
  have any meaning.

# In development

Changes below this point are in development.

## API Version 2

At the time of writing, this version is expected to be introduced in `rippled` version 2.0.

Currently (prior to the release of 2.0), it is available as a "beta" version, meaning it can be enabled with a config setting in `rippled.cfg`:

```
[beta_rpc_api]
1
```

Since `api_version` 2 is in "beta", breaking changes to V2 can currently be made at any time.

#### Removed methods

In API version 2, the following methods are no longer available:

- `tx_history` - Instead, use other methods such as `account_tx` or `ledger` with the `transactions` field set to `true`.
- `ledger_header` - Instead, use the `ledger` method.

#### Modifications to account_info response in V2

- `signer_lists` is returned in the root of the response. Previously, in API version 1, it was nested under `account_data`. (https://github.com/XRPLF/rippled/pull/3770)
- When using an invalid `signer_lists` value, the API now returns an "invalidParams" error. (https://github.com/XRPLF/rippled/pull/4585)
  - (`signer_lists` must be a boolean. In API version 1, strings are accepted and may return a normal response - as if `signer_lists` were `true`.)

#### Modifications to [account_tx](https://xrpl.org/account_tx.html#account_tx) response in V2

- Using `ledger_index_min`, `ledger_index_max`, and `ledger_index` returns `invalidParams` because if you use `ledger_index_min` or `ledger_index_max`, then it does not make sense to also specify `ledger_index`. Previously, in API version 1, no error was returned. (https://github.com/XRPLF/rippled/pull/4571)
  - The same applies for `ledger_index_min`, `ledger_index_max`, and `ledger_hash`. (https://github.com/XRPLF/rippled/issues/4545#issuecomment-1565065579)
- Using a `ledger_index_min` or `ledger_index_max` beyond the range of ledgers that the server has:
  - returns `lgrIdxMalformed` in API version 2. (https://github.com/XRPLF/rippled/issues/4288)
  - Previously, in API version 1, no error was returned.

##### In progress

- Attempting to use a non-boolean value (such as a string) for the `binary` or `forward` parameters returns `invalidParams` (`rpcINVALID_PARAMS`). Previously, in API version 1, no error was returned. (https://github.com/XRPLF/rippled/pull/4620)

#### Modifications to [noripple_check](https://xrpl.org/noripple_check.html#noripple_check) response in V2

##### In progress

- Attempting to use a non-boolean value (such as a string) for the `transactions` parameter returns `invalidParams` (`rpcINVALID_PARAMS`). Previously, in API version 1, no error was returned. (https://github.com/XRPLF/rippled/pull/4620)

##### In progress

- In `Payment` transaction type, JSON RPC field `Amount` is renamed to `DeliverMax`. To enable smooth client transition, `Amount` is still handled, as described below:
  - On JSON RPC input (e.g. `submit_multisigned` etc. methods), `Amount` is recognized as an alias to `DeliverMax` for both API version 1 and version 2 clients.
  - On JSON RPC input, submitting both `Amount` and `DeliverMax` fields is allowed _only_ if they are identical; otherwise such input is rejected with `rpcINVALID_PARAMS` error.
  - On JSON RPC output (e.g. `subscribe`, `account_tx` etc. methods), `DeliverMax` is present in both API version 1 and version 2.
  - On JSON RPC output, `Amount` is only present in API version 1 and _not_ in version 2.


# Unit tests for API changes

The following information is useful to developers contributing to this project:

The purpose of unit tests is to catch bugs and prevent regressions. In general, it often makes sense to create a test function when there is a breaking change to the API. For APIs that have changed in a new API version, the tests should be modified so that both the prior version and the new version are properly tested.

To take one example: for `account_info` version 1, WebSocket and JSON-RPC behavior should be tested. The latest API version, i.e. API version 2, should be tested over WebSocket, JSON-RPC, and command line.
