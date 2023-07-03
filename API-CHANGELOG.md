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

In [the response to the `account_info` command](https://xrpl.org/account_info.html#response-format), there is `account_data` - which is supposed to be an `AccountRoot` object - and `signer_lists` is in this object. However, the docs say that `signer_lists` should be at the root level of the reponse - and this makes more sense, since signer lists are not part of the AccountRoot object. (First reported in [xrpl-dev-portal#938](https://github.com/XRPLF/xrpl-dev-portal/issues/938).) Thanks to [rippled#3770](https://github.com/XRPLF/rippled/pull/3770), this field will be moved in `api_version: 2`.

#### server_info - network_id

The `network_id` field was added in the `server_info` response in version 1.5.0 (2019), but it was not returned in [reporting mode](https://xrpl.org/rippled-server-modes.html#reporting-mode).

## XRP Ledger version 1.11.0

### Breaking changes in 1.11

- Added the ability to mark amendments as obsolete. For the `feature` admin API, there is a new possible value for the `vetoed` field. ([#4291](https://github.com/XRPLF/rippled/pull/4291))
- The API now won't accept seeds or public keys in place of account addresses. ([#4404](https://github.com/XRPLF/rippled/pull/4404))
- For the `ledger_data` method, when all entries are filtered out, the API now returns an empty list (an empty array, `[]`). (Previously, it would return `null`.) While this is technically a breaking change, the new behavior is consistent with the documentation, so this is considered only a bug fix. ([#4398](https://github.com/XRPLF/rippled/pull/4398))
- If and when the `fixNFTokenRemint` amendment activates, there will be a new AccountRoot field, `FirstNFTSequence`. This field is set to the current account sequence when the account issues their first NFT. If an account has not issued any NFTs, then the field is not set. ([#4406](https://github.com/XRPLF/rippled/pull/4406))
  - There is a new account deletion restriction: an account can only be deleted if `FirstNFTSequence` + `MintedNFTokens` + `256` is less than the current ledger sequence.

### Additions and bug fixes in 1.11

- Added `nftoken_id`, `nftoken_ids` and `offer_id` meta fields into NFT `tx` and `account_tx` responses. (https://github.com/XRPLF/rippled/pull/4447)
- Added an `account_flags` object to the `account_info` method response. (https://github.com/XRPLF/rippled/pull/4459)
- Added `NFTokenPages` to the `account_objects` RPC. (https://github.com/XRPLF/rippled/pull/4352)
- Fixed: `marker` returned from the `account_lines` command would not work on subsequent commands. (https://github.com/XRPLF/rippled/pull/4361)
