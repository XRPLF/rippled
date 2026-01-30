# API Changelog

This changelog is intended to list all updates to the [public API methods](https://xrpl.org/public-api-methods.html).

For info about how [API versioning](https://xrpl.org/request-formatting.html#api-versioning) works, including examples, please view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155). API versioning ensures existing integrations and users continue to receive existing behavior, while those that request a higher API version will experience new behavior.

The API version controls the API behavior you see. This includes what properties you see in responses, what parameters you're permitted to send in requests, and so on. You specify the API version in each of your requests. When a breaking change is introduced to the `rippled` API, a new version is released. To avoid breaking your code, you should set (or increase) your version when you're ready to upgrade.

The [commandline](https://xrpl.org/docs/references/http-websocket-apis/api-conventions/request-formatting/#commandline-format) always uses the latest API version. The command line is intended for ad-hoc usage by humans, not programs or automated scripts. The command line is not meant for use in production code.

For a log of breaking changes, see the **API Version [number]** headings. In general, breaking changes are associated with a particular API Version number. For non-breaking changes, scroll to the **XRP Ledger version [x.y.z]** headings. Non-breaking changes are associated with a particular XRP Ledger (`rippled`) release.

## API Version 3 (Beta)

API version 3 is currently a beta API. It requires enabling `[beta_rpc_api]` in the rippled configuration to use. See [API-VERSION-3.md](API-VERSION-3.md) for the full list of changes in API version 3.

## API Version 2

API version 2 is available in `rippled` version 2.0.0 and later. See [API-VERSION-2.md](API-VERSION-2.md) for the full list of changes in API version 2.

## API Version 1

This version is supported by all `rippled` versions. For WebSocket and HTTP JSON-RPC requests, it is currently the default API version used when no `api_version` is specified.

## XRP Ledger server version 3.1.0

[Version 3.1.0](https://github.com/XRPLF/rippled/releases/tag/3.1.0) was released on Jan 27, 2026.

### Additions in 3.1.0

- `vault_info`: New RPC method to retrieve information about a specific vault (part of XLS-66 Lending Protocol). ([#6156](https://github.com/XRPLF/rippled/pull/6156))

## XRP Ledger server version 3.0.0

[Version 3.0.0](https://github.com/XRPLF/rippled/releases/tag/3.0.0) was released on Dec 9, 2025.

### Additions in 3.0.0

- `ledger_entry`: Supports all ledger entry types with dedicated parsers. ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `ledger_entry`: New error codes `entryNotFound` and `unexpectedLedgerType` for more specific error handling. ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `ledger_entry`: Improved error messages with more context (e.g., specifying which field is invalid or missing). ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `ledger_entry`: Assorted bug fixes in RPC processing. ([#5237](https://github.com/XRPLF/rippled/pull/5237))
- `simulate`: Supports additional metadata in the response. ([#5754](https://github.com/XRPLF/rippled/pull/5754))

## XRP Ledger server version 2.6.2

[Version 2.6.2](https://github.com/XRPLF/rippled/releases/tag/2.6.2) was released on Nov 19, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.6.1

[Version 2.6.1](https://github.com/XRPLF/rippled/releases/tag/2.6.1) was released on Sep 30, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.6.0

[Version 2.6.0](https://github.com/XRPLF/rippled/releases/tag/2.6.0) was released on Aug 27, 2025.

### Additions in 2.6.0

- `account_info`: Added `allowTrustLineLocking` flag in response. ([#5525](https://github.com/XRPLF/rippled/pull/5525))
- `ledger`: Removed the type filter from the RPC command. ([#4934](https://github.com/XRPLF/rippled/pull/4934))
- `subscribe` (`validations` stream): `network_id` is now included. ([#5579](https://github.com/XRPLF/rippled/pull/5579))
- `subscribe` (`transactions` stream): `nftoken_id`, `nftoken_ids`, and `offer_id` are now included in transaction metadata. ([#5230](https://github.com/XRPLF/rippled/pull/5230))

## XRP Ledger server version 2.5.1

[Version 2.5.1](https://github.com/XRPLF/rippled/releases/tag/2.5.1) was released on Sep 17, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.5.0

[Version 2.5.0](https://github.com/XRPLF/rippled/releases/tag/2.5.0) was released on Jun 24, 2025.

### Additions and bugfixes in 2.5.0

- `tx`: Added `ctid` field to the response and improved error handling. ([#4738](https://github.com/XRPLF/rippled/pull/4738))
- `ledger_entry`: Improved error messages in `permissioned_domain`. ([#5344](https://github.com/XRPLF/rippled/pull/5344))
- `simulate`: Improved multi-sign usage. ([#5479](https://github.com/XRPLF/rippled/pull/5479))
- `channel_authorize`: If `signing_support` is not enabled in the config, the RPC is disabled. ([#5385](https://github.com/XRPLF/rippled/pull/5385))
- `subscribe` (admin): Removed webhook queue limit to prevent dropping notifications; reduced HTTP timeout from 10 minutes to 30 seconds. ([#5163](https://github.com/XRPLF/rippled/pull/5163))
- `ledger_data` (gRPC): Fixed crashing issue with some invalid markers. ([#5137](https://github.com/XRPLF/rippled/pull/5137))
- `account_lines`: Fixed error with `no_ripple` and `no_ripple_peer` sometimes showing up incorrectly. ([#5345](https://github.com/XRPLF/rippled/pull/5345))
- `account_tx`: Fixed issue with incorrect CTIDs. ([#5408](https://github.com/XRPLF/rippled/pull/5408))

## XRP Ledger server version 2.4.0

[Version 2.4.0](https://github.com/XRPLF/rippled/releases/tag/2.4.0) was released on March 4, 2025.

### Additions and bugfixes in 2.4.0

- `simulate`: A new RPC that executes a [dry run of a transaction submission](https://github.com/XRPLF/XRPL-Standards/tree/master/XLS-0069d-simulate#2-rpc-simulate). ([#5069](https://github.com/XRPLF/rippled/pull/5069))
- Signing methods (`sign`, `sign_for`, `submit`): Autofill fees better, properly handle transactions without a base fee, and autofill the `NetworkID` field. ([#5069](https://github.com/XRPLF/rippled/pull/5069))
- `ledger_entry`: `state` is added as an alias for `ripple_state`. ([#5199](https://github.com/XRPLF/rippled/pull/5199))
- `ledger`, `ledger_data`, `account_objects`: Support filtering ledger entry types by their canonical names (case-insensitive). ([#5271](https://github.com/XRPLF/rippled/pull/5271))
- `validators`: Added new field `validator_list_threshold` in response. ([#5112](https://github.com/XRPLF/rippled/pull/5112))
- `server_info`: Added git commit hash info on admin connection. ([#5225](https://github.com/XRPLF/rippled/pull/5225))
- `server_definitions`: Changed larger `UInt` serialized types to `Hash`. ([#5231](https://github.com/XRPLF/rippled/pull/5231))

## XRP Ledger server version 2.3.1

[Version 2.3.1](https://github.com/XRPLF/rippled/releases/tag/2.3.1) was released on Jan 29, 2025.

This release contains bug fixes only and no API changes.

## XRP Ledger server version 2.3.0

[Version 2.3.0](https://github.com/XRPLF/rippled/releases/tag/2.3.0) was released on Nov 25, 2024.

### Breaking changes in 2.3.0

- `book_changes`: If the requested ledger version is not available on this node, a `ledgerNotFound` error is returned and the node does not attempt to acquire the ledger from the p2p network (as with other non-admin RPCs). Admins can still attempt to retrieve old ledgers with the `ledger_request` RPC.

### Additions and bugfixes in 2.3.0

- `book_changes`: Returns a `validated` field in its response. ([#5096](https://github.com/XRPLF/rippled/pull/5096))
- `book_changes`: Accepts shortcut strings (`current`, `closed`, `validated`) for the `ledger_index` parameter. ([#5096](https://github.com/XRPLF/rippled/pull/5096))
- `server_definitions`: Include `index` in response. ([#5190](https://github.com/XRPLF/rippled/pull/5190))
- `account_nfts`: Fix issue where unassociated marker would return incorrect results. ([#5045](https://github.com/XRPLF/rippled/pull/5045))
- `account_objects`: Fix issue where invalid marker would not return an error. ([#5046](https://github.com/XRPLF/rippled/pull/5046))
- `account_objects`: Disallow filtering by ledger entry types that an account cannot hold. ([#5056](https://github.com/XRPLF/rippled/pull/5056))
- `tx`: Allow lowercase CTID. ([#5049](https://github.com/XRPLF/rippled/pull/5049))
- `feature`: Better error handling for invalid values of `feature`. ([#5063](https://github.com/XRPLF/rippled/pull/5063))

## XRP Ledger server version 2.2.0

[Version 2.2.0](https://github.com/XRPLF/rippled/releases/tag/2.2.0) was released on Jun 5, 2024. The following additions are non-breaking (because they are purely additive):

- `feature`: Add a non-admin mode for users. (It was previously only available to admin connections.) The method returns an updated list of amendments, including their names and other information. ([#4781](https://github.com/XRPLF/rippled/pull/4781))

## XRP Ledger server version 2.0.1

[Version 2.0.1](https://github.com/XRPLF/rippled/releases/tag/2.0.1) was released on Jan 29, 2024. The following additions are non-breaking:

- `path_find`: Fixes unbounded memory growth. ([#4822](https://github.com/XRPLF/rippled/pull/4822))

## XRP Ledger server version 2.0.0

[Version 2.0.0](https://github.com/XRPLF/rippled/releases/tag/2.0.0) was released on Jan 9, 2024. The following additions are non-breaking (because they are purely additive):

- `server_definitions`: A new RPC that generates a `definitions.json`-like output that can be used in XRPL libraries.
- In `Payment` transactions, `DeliverMax` has been added. This is a replacement for the `Amount` field, which should not be used. Typically, the `delivered_amount` (in transaction metadata) should be used. To ease the transition, `DeliverMax` is present regardless of API version, since adding a field is non-breaking.
- API version 2 has been moved from beta to supported, meaning that it is generally available (regardless of the `beta_rpc_api` setting). The full list of changes is in [API-VERSION-2.md](API-VERSION-2.md).

## XRP Ledger server version 1.12.0

[Version 1.12.0](https://github.com/XRPLF/rippled/releases/tag/1.12.0) was released on Sep 6, 2023. The following additions are non-breaking (because they are purely additive):

- `server_info`: Added `ports`, an array which advertises the RPC and WebSocket ports. This information is also included in the `/crawl` endpoint (which calls `server_info` internally). `grpc` and `peer` ports are also included. ([#4427](https://github.com/XRPLF/rippled/pull/4427))
  - `ports` contains objects, each containing a `port` for the listening port (a number string), and a `protocol` array listing the supported protocols on that port.
  - This allows crawlers to build a more detailed topology without needing to port-scan nodes.
  - (For peers and other non-admin clients, the info about admin ports is excluded.)
- Clawback: The following additions are gated by the Clawback amendment (`featureClawback`). ([#4553](https://github.com/XRPLF/rippled/pull/4553))
  - Adds an [AccountRoot flag](https://xrpl.org/accountroot.html#accountroot-flags) called `lsfAllowTrustLineClawback`. ([#4617](https://github.com/XRPLF/rippled/pull/4617))
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
    - tecAMM_EMPTY: AMM is in empty state. Transaction requires AMM in non-empty state (LP tokens > 0).
    - tecAMM_NOT_EMPTY: AMM is not in empty state. Transaction requires AMM in empty state (LP tokens == 0).
    - tecAMM_ACCOUNT: AMM account. Clawback of AMM account.
    - tecINCOMPLETE: Some work was completed, but more submissions required to finish. AMMDelete partially deletes the trustlines.

## XRP Ledger server version 1.11.0

[Version 1.11.0](https://github.com/XRPLF/rippled/releases/tag/1.11.0) was released on Jun 20, 2023.

### Breaking changes in 1.11

- Added the ability to mark amendments as obsolete. For the `feature` admin API, there is a new possible value for the `vetoed` field. ([#4291](https://github.com/XRPLF/rippled/pull/4291))
  - The value of `vetoed` can now be `true`, `false`, or `"Obsolete"`.
- Removed the acceptance of seeds or public keys in place of account addresses. ([#4404](https://github.com/XRPLF/rippled/pull/4404))
  - This simplifies the API and encourages better security practices (i.e. seeds should never be sent over the network).
- For the `ledger_data` method, when all entries are filtered out, the `state` field of the response is now an empty list (in other words, an empty array, `[]`). (Previously, it would return `null`.) While this is technically a breaking change, the new behavior is consistent with the documentation, so this is considered only a bug fix. ([#4398](https://github.com/XRPLF/rippled/pull/4398))
- If and when the `fixNFTokenRemint` amendment activates, there will be a new AccountRoot field, `FirstNFTSequence`. This field is set to the current account sequence when the account issues their first NFT. If an account has not issued any NFTs, then the field is not set. ([#4406](https://github.com/XRPLF/rippled/pull/4406))
  - There is a new account deletion restriction: an account can only be deleted if `FirstNFTSequence` + `MintedNFTokens` + `256` is less than the current ledger sequence.
  - This is potentially a breaking change if clients have logic for determining whether an account can be deleted.
- NetworkID
  - For sidechains and networks with a network ID greater than 1024, there is a new [transaction common field](https://xrpl.org/transaction-common-fields.html), `NetworkID`. ([#4370](https://github.com/XRPLF/rippled/pull/4370))
    - This field helps to prevent replay attacks and is now required for chains whose network ID is 1025 or higher.
    - The field must be omitted for Mainnet, so there is no change for Mainnet users.
  - There are three new local error codes:
    - `telNETWORK_ID_MAKES_TX_NON_CANONICAL`: a `NetworkID` is present but the chain's network ID is less than 1025. Remove the field from the transaction, and try again.
    - `telREQUIRES_NETWORK_ID`: a `NetworkID` is required, but is not present. Add the field to the transaction, and try again.
    - `telWRONG_NETWORK`: a `NetworkID` is specified, but it is for a different network. Submit the transaction to a different server which is connected to the correct network.

### Additions and bug fixes in 1.11

- Added `nftoken_id`, `nftoken_ids` and `offer_id` meta fields into NFT `tx` and `account_tx` responses. ([#4447](https://github.com/XRPLF/rippled/pull/4447))
- Added an `account_flags` object to the `account_info` method response. ([#4459](https://github.com/XRPLF/rippled/pull/4459))
- Added `NFTokenPages` to the `account_objects` RPC. ([#4352](https://github.com/XRPLF/rippled/pull/4352))
- Fixed: `marker` returned from the `account_lines` command would not work on subsequent commands. ([#4361](https://github.com/XRPLF/rippled/pull/4361))

## XRP Ledger server version 1.10.0

[Version 1.10.0](https://github.com/XRPLF/rippled/releases/tag/1.10.0)
was released on Mar 14, 2023.

### Breaking changes in 1.10

- If the `XRPFees` feature is enabled, the `fee_ref` field will be
  removed from the [ledger subscription stream](https://xrpl.org/subscribe.html#ledger-stream), because it will no longer
  have any meaning.

# Unit tests for API changes

The following information is useful to developers contributing to this project:

The purpose of unit tests is to catch bugs and prevent regressions. In general, it often makes sense to create a test function when there is a breaking change to the API. For APIs that have changed in a new API version, the tests should be modified so that both the prior version and the new version are properly tested.

To take one example: for `account_info` version 1, WebSocket and JSON-RPC behavior should be tested. The latest API version, i.e. API version 2, should be tested over WebSocket, JSON-RPC, and command line.
