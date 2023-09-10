# API Changelog

This changelog is intended to list all updates to the API.

For info about how API versioning works, view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155).

The API version controls the API behavior you see. This includes what properties you see in responses, what parameters you're permitted to send in requests, and so on. You specify the API version in each of your requests. When a breaking change is introduced to the `rippled` API, a new version is released. To avoid breaking your code, you should set (or increase) your version when you're ready to upgrade.

For a log of breaking changes, see the **API Version [number]** headings. Breaking changes are associated with a particular API Version number. For non-breaking changes, scroll to the **XRP Ledger version [x.y.z]** headings. Non-breaking changes are associated with a particular XRP Ledger (`rippled`) release.

## API Version 2
This version will be supported by `rippled` version 1.12.

#### V2 account_info response

`signer_lists` is returned in the root of the response, instead of being nested under `account_data` (as it was in API version 1). ([#3770](https://github.com/XRPLF/rippled/pull/3770))

## API Version 1
This version is supported by `rippled` version 1.11.

### Idiosyncrasies

#### V1 account_info response

In [the response to the `account_info` command](https://xrpl.org/account_info.html#response-format), there is `account_data` - which is supposed to be an `AccountRoot` object - and `signer_lists` is in this object. However, the docs say that `signer_lists` should be at the root level of the reponse - and this makes sense, since signer lists are not part of the AccountRoot object. (First reported in [xrpl-dev-portal#938](https://github.com/XRPLF/xrpl-dev-portal/issues/938).) Thanks to [rippled#3770](https://github.com/XRPLF/rippled/pull/3770), this field will be moved in `api_version: 2`, to the root level of the response.

#### server_info - network_id

The `network_id` field was added in the `server_info` response in version 1.5.0 (2019), but it was not returned in [reporting mode](https://xrpl.org/rippled-server-modes.html#reporting-mode).

## XRP Ledger version 1.12.0

Version 1.12.0 is in development.

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
       -  tecAMM_BALANCE: AMM has invalid balance. Calculated balances greater than the current pool balances.
       -  tecAMM_FAILED: AMM transaction failed. Fails due to a processing failure.
       -  tecAMM_INVALID_TOKENS: AMM invalid LP tokens. Invalid input values, format, or calculated values.
       -  tecAMM_EMPTY: AMM is in empty state. Transaction expects AMM in non-empty state (LP tokens > 0).
       -  tecAMM_NOT_EMPTY: AMM is not in empty state. Transaction expects AMM in empty state (LP tokens == 0).
       -  tecAMM_ACCOUNT: AMM account. Clawback of AMM account.
       -  tecINCOMPLETE: Some work was completed, but more submissions required to finish. AMMDelete partially deletes the trustlines.

## XRP Ledger version 1.11.0

[Version 1.11.0](https://github.com/XRPLF/rippled/releases/tag/1.11.0) was released on Jun 20, 2023.

### Breaking changes in 1.11

- Added the ability to mark amendments as obsolete. For the `feature` admin API, there is a new possible value for the `vetoed` field. ([#4291](https://github.com/XRPLF/rippled/pull/4291))
- The API now won't accept seeds or public keys in place of account addresses. ([#4404](https://github.com/XRPLF/rippled/pull/4404))
- For the `ledger_data` method, when all entries are filtered out, the API now returns an empty list (an empty array, `[]`). (Previously, it would return `null`.) While this is technically a breaking change, the new behavior is consistent with the documentation, so this is considered only a bug fix. ([#4398](https://github.com/XRPLF/rippled/pull/4398))
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
