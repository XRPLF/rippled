# API Changelog
Keep track of every change to the API.

This changelog is intended to list all updates to the API.

For info about how API verioning works, view the [XLS-22d spec](https://github.com/XRPLF/XRPL-Standards/discussions/54). For details about the implementation of API versioning, view the [implementation PR](https://github.com/XRPLF/rippled/pull/3155).

The API version controls the API behavior you see. This includes what properties you see in responses, what parameters you're permitted to send in requests, and so on. You specify the API version in each of your requests. When a breaking change is introduced to the `rippled` API, a new version is released. To avoid breaking your code, you should set (or increase) your version when you're ready to upgrade.

## API Version 2
This version is supported by `rippled` version 1.12.

#### account_info response

`signer_lists` is returned in the root of the response, instead of being nested under `account_data` (as it was in API version 1). ([#3770](https://github.com/XRPLF/rippled/pull/3770))

## API Version 1
This version is supported by `rippled` version 1.11.

### Idiosyncrasies

#### account_info response

In [the response to the `account_info` command](https://xrpl.org/account_info.html#response-format), there is `account_data` - which is supposed to be an `AccountRoot` object - and `signer_lists` is in this object. However, the docs say that `signer_lists` should be at the root level of the reponse - and this makes more sense, since signer lists are not part of the AccountRoot object. (First reported in [xrpl-dev-portal#938](https://github.com/XRPLF/xrpl-dev-portal/issues/938).) Thanks to [rippled#3770](https://github.com/XRPLF/rippled/pull/3770), this field will be moved in `api_version: 2`.
