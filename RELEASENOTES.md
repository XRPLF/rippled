# Release Notes

![XRP](docs/images/xrp-text-mark-black-small@2x.png)

This document contains the release notes for `rippled`, the reference server implementation of the XRP Ledger protocol. To learn more about how to build, run or update a `rippled` server, visit https://xrpl.org/install-rippled.html

Have new ideas? Need help with setting up your node? [Please open an issue here](https://github.com/xrplf/rippled/issues/new/choose).

# Introducing XRP Ledger version 2.0.0

Version 2.0.0 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available. This release adds new features and bug fixes, and introduces these amendments:

- `DID`
- `XChainBridge`
- `fixDisallowIncomingV1`
- `fixFillOrKill`

[Sign Up for Future Release Announcements](https://groups.google.com/g/ripple-server)

<!-- BREAK -->


## Action Required

Four new amendments are now open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, upgrade to version 2.0.0 by January 22, 2024 to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.


## Changelog


### Amendments, New Features, and Changes
(These are changes which may impact or be useful to end users. For example, you may be able to update your code/workflow to take advantage of these changes.)

- **XChainBridge**: Introduces cross-chain bridges, enabling interoperability between the XRP Ledger and sidechains. [#4292](https://github.com/XRPLF/rippled/pull/4292)

- **DID**: Introduces decentralized identifiers. [#4636](https://github.com/XRPLF/rippled/pull/4636)

- **fixDisallowIncomingV1**: Fixes an issue that occurs when users try to authorize a trustline while the `lsfDisallowIncomingTrustline` flag is enabled on their account. [#4721](https://github.com/XRPLF/rippled/pull/4721)

- **fixFillOrKill**: Fixes an issue introduced in the `flowCross` amendment. The `tfFillOrKill` and `tfSell` flags are now properly handled to allow offers to cross in certain scenarios. [#4694](https://github.com/XRPLF/rippled/pull/4694)

- **API v2 released with these changes:**

  - Accepts currency codes in ASCII, using the full alphabet. [#4566](https://github.com/XRPLF/rippled/pull/4566)
  - Added test to verify the `check` field is a string. [#4630](https://github.com/XRPLF/rippled/pull/4630)
  - Added errors for malformed `account_tx` and `noripple_check` fields. [#4620](https://github.com/XRPLF/rippled/pull/4620)
  - Added errors for malformed `gateway_balances` and `channel_authorize` requests. [#4618](https://github.com/XRPLF/rippled/pull/4618)
  - Added a `DeliverMax` alias to `Amount` and removed `Amount`. [#4733](https://github.com/XRPLF/rippled/pull/4733)
  - Removed `tx_history` and `ledger_header` methods. Also updated `RPC::Handler` to allow for version-specific methods. [#4759](https://github.com/XRPLF/rippled/pull/4759)
  - Standardized the JSON serialization format of transactions. [#4727](https://github.com/XRPLF/rippled/issues/4727)
  - Bumped API support to v2, but kept the command-line interface for `rippled` and unit tests at v1. [#4803](https://github.com/XRPLF/rippled/pull/4803)
  - Standardized `ledger_index` to return as a number. [#4820](https://github.com/XRPLF/rippled/pull/4820)

- Added a `server_definitions` command that returns an SDK-compatible `definitions.json` file, generated from the `rippled` instance currently running. [#4703](https://github.com/XRPLF/rippled/pull/4703)

- Improved unit test command line input and run times. [#4634](https://github.com/XRPLF/rippled/pull/4634)

- Added the link compression setting to the the `rippled-example.cfg` file. [#4753](https://github.com/XRPLF/rippled/pull/4753)

- Changed the reserved hook error code name from `tecHOOK_ERROR` to `tecHOOK_REJECTED`. [#4559](https://github.com/XRPLF/rippled/pull/4559)


### Bug Fixes and Performance Improvements
(These are behind-the-scenes improvements, such as internal changes to the code, which are not expected to impact end users.)

- Simplified `TxFormats` common fields logic. [#4637](https://github.com/XRPLF/rippled/pull/4637)

- Improved transaction throughput by asynchronously writing batches to *NuDB*. [#4503](https://github.com/XRPLF/rippled/pull/4503)

- Removed 2 unused functions. [#4708](https://github.com/XRPLF/rippled/pull/4708)

- Removed an unused variable that caused clang 14 build errors. [#4672](https://github.com/XRPLF/rippled/pull/4672)

- Fixed comment about return value of `LedgerHistory::fixIndex`. [#4574](https://github.com/XRPLF/rippled/pull/4574)

- Updated `secp256k1` to 0.3.2. [#4653](https://github.com/XRPLF/rippled/pull/4653)

- Removed built-in SNTP clock issues. [#4628](https://github.com/XRPLF/rippled/pull/4628)

- Fixed amendment flapping. This issue usually occurred when an amendment was on the verge of gaining majority, but a validator not in favor of the amendment went offline. [#4410](https://github.com/XRPLF/rippled/pull/4410)

- Fixed asan stack-use-after-scope issue. [#4676](https://github.com/XRPLF/rippled/pull/4676)

- Transactions and pseudo-transactions share the same `commonFields` again. [#4715](https://github.com/XRPLF/rippled/pull/4715)

- Reduced boilerplate in `applySteps.cpp`. When a new transactor is added, only one function needs to be modified now. [#4710](https://github.com/XRPLF/rippled/pull/4710)

- Removed an incorrect assert. [#4743](https://github.com/XRPLF/rippled/pull/4743)

- Replaced some asserts in `PeerFinder::Logic` with `LogicError` to better indicate the nature of server crashes. [#4562](https://github.com/XRPLF/rippled/pull/4562)

- Fixed an issue with enabling new amendments on a network with an ID greater than 1024. [#4737](https://github.com/XRPLF/rippled/pull/4737)


### Docs and Build System

- Updated `rippled-example.cfg` docs to clarify usage of *ssl_cert* vs *ssl_chain*. [#4667](https://github.com/XRPLF/rippled/pull/4667)

- Updated `BUILD.md`:
    - Made the `environment.md` link easier to find. Also made it easier to find platform-specific info. [#4507](https://github.com/XRPLF/rippled/pull/4507)
    - Fixed typo. [#4718](https://github.com/XRPLF/rippled/pull/4718)
    - Updated the minimum compiler requirements. [#4700](https://github.com/XRPLF/rippled/pull/4700)
    - Added note about enabling `XRPFees`. [#4741](https://github.com/XRPLF/rippled/pull/4741)

- Updated `API-CHANGELOG.md`:
    - Explained API v2 is releasing with `rippled` 2.0.0. [#4633](https://github.com/XRPLF/rippled/pull/4633)
    - Clarified the location of the `signer_lists` field in the `account_info` response for API v2. [#4724](https://github.com/XRPLF/rippled/pull/4724)
    - Added documentation for the new `DeliverMax` field. [#4784](https://github.com/XRPLF/rippled/pull/4784)
    - Removed references to API v2 being "in progress" and "in beta". [#4828](https://github.com/XRPLF/rippled/pull/4828)
    - Clarified that all breaking API changes will now occur in API v3 or later. [#4773](https://github.com/XRPLF/rippled/pull/4773)

- Fixed a mistake in the overlay README. [#4635](https://github.com/XRPLF/rippled/pull/4635)

- Fixed an early return from `RippledRelease.cmake` that prevented targets from being created during packaging. [#4707](https://github.com/XRPLF/rippled/pull/4707)

- Fixed a build error with Intel Macs. [#4632](https://github.com/XRPLF/rippled/pull/4632)

- Added `.build` to `.gitignore`. [#4722](https://github.com/XRPLF/rippled/pull/4722)

- Fixed a `uint is not universally defined` Windows build error. [#4731](https://github.com/XRPLF/rippled/pull/4731)

- Reenabled Windows CI build with Artifactory support. [#4596](https://github.com/XRPLF/rippled/pull/4596)

- Fixed output of remote step in Nix workflow. [#4746](https://github.com/XRPLF/rippled/pull/4746)

- Fixed a broken link in `conan.md`. [#4740](https://github.com/XRPLF/rippled/pull/4740)

- Added a `python` call to fix the `pip` upgrade command in Windows CI. [#4768](https://github.com/XRPLF/rippled/pull/4768)

- Added an API Impact section to `pull_request_template.md`. [#4757](https://github.com/XRPLF/rippled/pull/4757)

- Set permissions for the Doxygen workflow. [#4756](https://github.com/XRPLF/rippled/pull/4756)

- Switched to Unity builds to speed up Windows CI. [#4780](https://github.com/XRPLF/rippled/pull/4780)

- Clarified what makes concensus healthy in `FeeEscalation.md`. [#4729](https://github.com/XRPLF/rippled/pull/4729)

- Removed a dependency on the <ranges> header for unit tests. [#4788](https://github.com/XRPLF/rippled/pull/4788)

- Fixed a clang `unused-but-set-variable` warning. [#4677](https://github.com/XRPLF/rippled/pull/4677)

- Removed an unused Dockerfile. [#4791](https://github.com/XRPLF/rippled/pull/4791)

- Fixed unit tests to work with API v2. [#4785](https://github.com/XRPLF/rippled/pull/4785)

- Added support for the mold linker on Linux. [#4807](https://github.com/XRPLF/rippled/pull/4807)

- Updated Linux distribtuions `rippled` smoke tests run on. [#4813](https://github.com/XRPLF/rippled/pull/4813)

- Added codename `bookworm` to the distribution matrix during Artifactory uploads, enabling Debian 12 clients to install `rippled` packages. [#4836](https://github.com/XRPLF/rippled/pull/4836)

- Added a workaround for compilation errors with GCC 13 and other compilers relying on libstdc++ version 13. [#4817](https://github.com/XRPLF/rippled/pull/4817)

- Fixed a minor typo in the code comments of `AMMCreate.h`. [4821](https://github.com/XRPLF/rippled/pull/4821)


### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome all contributions and invite everyone to join the community of XRP Ledger developers to help build the Internet of Value.


## Credits

The following people contributed directly to this release:

- Bronek Kozicki <brok@incorrekt.com>
- Chenna Keshava B S <21219765+ckeshava@users.noreply.github.com>
- Denis Angell <dangell@transia.co>
- Ed Hennis <ed@ripple.com>
- Elliot Lee <github.public@intelliot.com>
- Florent <36513774+florent-uzio@users.noreply.github.com>
- ForwardSlashBack <142098649+ForwardSlashBack@users.noreply.github.com>
- Gregory Tsipenyuk <gregtatcam@users.noreply.github.com>
- Howard Hinnant <howard.hinnant@gmail.com>
- Hussein Badakhchani <husseinb01@gmail.com>
- Jackson Mills <aim4math@gmail.com>
- John Freeman <jfreeman08@gmail.com>
- Manoj Doshi <mdoshi@ripple.com>
- Mark Pevec <mark.pevec@gmail.com>
- Mark Travis <mtrippled@users.noreply.github.com>
- Mayukha Vadari <mvadari@gmail.com>
- Michael Legleux <legleux@users.noreply.github.com>
- Nik Bougalis <nikb@bougalis.net>
- Peter Chen <34582813+PeterChen13579@users.noreply.github.com>
- Rome Reginelli <rome@ripple.com>
- Scott Determan <scott.determan@yahoo.com>
- Scott Schurr <scott@ripple.com>
- Sophia Xie <106177003+sophiax851@users.noreply.github.com>
- Stefan van Kessel <van_kessel@freenet.de>
- pwang200 <354723+pwang200@users.noreply.github.com>
- shichengsg002 <147461171+shichengsg002@users.noreply.github.com>
- sokkaofthewatertribe <140777955+sokkaofthewatertribe@users.noreply.github.com>

Bug Bounties and Responsible Disclosures:

We welcome reviews of the rippled code and urge researchers to responsibly disclose any issues they may find.

To report a bug, please send a detailed report to: <bugs@xrpl.org>


# Introducing XRP Ledger version 1.12.0

Version 1.12.0 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available. This release adds new features and bug fixes, and introduces these amendments:

- `AMM`
- `Clawback`
- `fixReducedOffersV1`

[Sign Up for Future Release Announcements](https://groups.google.com/g/ripple-server)

<!-- BREAK -->

## Action Required

Three new amendments are now open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, upgrade to version 1.12.0 by September 20, 2023 to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.


## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).

The XRPL Foundation publishes portable binaries, which are drop-in replacements for the `rippled` daemon. [See information and downloads for the portable binaries](https://github.com/XRPLF/rippled-portable-builds#portable-builds-of-the-rippled-server). This will work on most distributions, including Ubuntu 16.04, 18.04, 20.04, and 22.04; CentOS; and others. Please test and open issues on GitHub if there are problems.


## Changelog

### Amendments, New Features, and Changes
(These are changes which may impact or be useful to end users. For example, you may be able to update your code/workflow to take advantage of these changes.)

- **`AMM`**: Introduces an automated market maker (AMM) protocol to the XRP Ledger's decentralized exchange, enabling you to trade assets without a counterparty. For more information about AMMs, see: [Automated Market Maker](https://opensource.ripple.com/docs/xls-30d-amm/amm-uc/). [#4294](https://github.com/XRPLF/rippled/pull/4294)

- **`Clawback`**: Adds a setting, *Allow Clawback*, which lets an issuer recover, or _claw back_, tokens that they previously issued. Issuers cannot enable this setting if they have issued tokens already. For additional documentation on this feature, see: [#4553](https://github.com/XRPLF/rippled/pull/4553).

- **`fixReducedOffersV1`**: Reduces the occurrence of order books that are blocked by reduced offers. [#4512](https://github.com/XRPLF/rippled/pull/4512)

- Added WebSocket and RPC port info to `server_info` responses. [#4427](https://github.com/XRPLF/rippled/pull/4427)

- Removed the deprecated `accepted`, `seqNum`, `hash`, and `totalCoins` fields from the `ledger` method. [#4244](https://github.com/XRPLF/rippled/pull/4244)


### Bug Fixes and Performance Improvements
(These are behind-the-scenes improvements, such as internal changes to the code, which are not expected to impact end users.)

- Added a pre-commit hook that runs the clang-format linter locally before committing changes. To install this feature, see: [CONTRIBUTING](https://github.com/XRPLF/xrpl-dev-portal/blob/master/CONTRIBUTING.md). [#4599](https://github.com/XRPLF/rippled/pull/4599)

- In order to make it more straightforward to catch and handle overflows: changed the output type of the `mulDiv()` function from `std::pair<bool, uint64_t>` to `std::optional`. [#4243](https://github.com/XRPLF/rippled/pull/4243)

- Updated `Handler::Condition` enum values to make the code less brittle. [#4239](https://github.com/XRPLF/rippled/pull/4239)

- Renamed `ServerHandlerImp` to `ServerHandler`. [#4516](https://github.com/XRPLF/rippled/pull/4516), [#4592](https://github.com/XRPLF/rippled/pull/4592)

- Replaced hand-rolled code with `std::from_chars` for better maintainability. [#4473](https://github.com/XRPLF/rippled/pull/4473)

- Removed an unused `TypedField` move constructor. [#4567](https://github.com/XRPLF/rippled/pull/4567)


### Docs and Build System

- Updated checkout versions to resolve warnings during GitHub jobs. [#4598](https://github.com/XRPLF/rippled/pull/4598)

- Fixed an issue with the Debian package build. [#4591](https://github.com/XRPLF/rippled/pull/4591)

- Updated build instructions with additional steps to take after updating dependencies. [#4623](https://github.com/XRPLF/rippled/pull/4623)

- Updated contributing doc to clarify that beta releases should also be pushed to the `release` branch. [#4589](https://github.com/XRPLF/rippled/pull/4589)

- Enabled the `BETA_RPC_API` flag in the default unit tests config, making the API v2 (beta) available to unit tests. [#4573](https://github.com/XRPLF/rippled/pull/4573)

- Conan dependency management.
  - Fixed package definitions for Conan. [#4485](https://github.com/XRPLF/rippled/pull/4485)
  - Updated build dependencies to the most recent versions in Conan Center. [#4595](https://github.com/XRPLF/rippled/pull/4595)
  - Updated Conan recipe for NuDB. [#4615](https://github.com/XRPLF/rippled/pull/4615)

- Added binary hardening and linker flags to enhance security during the build process. [#4603](https://github.com/XRPLF/rippled/pull/4603)

- Added an Artifactory to the `nix` workflow to improve build times. [#4556](https://github.com/XRPLF/rippled/pull/4556)

- Added quality-of-life improvements to workflows, using new [concurrency control](https://docs.github.com/en/actions/using-jobs/using-concurrency) features. [#4597](https://github.com/XRPLF/rippled/pull/4597)


[Full Commit Log](https://github.com/XRPLF/rippled/compare/1.11.0...1.12.0)


### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome all contributions and invite everyone to join the community of XRP Ledger developers to help build the Internet of Value.


## Credits

The following people contributed directly to this release:

- Alphonse N. Mousse <39067955+a-noni-mousse@users.noreply.github.com>
- Arihant Kothari <arihantkothari17@gmail.com>
- Chenna Keshava B S <21219765+ckeshava@users.noreply.github.com>
- Denis Angell <dangell@transia.co>
- Ed Hennis <ed@ripple.com>
- Elliot Lee <github.public@intelliot.com>
- Gregory Tsipenyuk <gregtatcam@users.noreply.github.com>
- Howard Hinnant <howard.hinnant@gmail.com>
- Ikko Eltociear Ashimine <eltociear@gmail.com>
- John Freeman <jfreeman08@gmail.com>
- Manoj Doshi <mdoshi@ripple.com>
- Mark Travis <mtravis@ripple.com>
- Mayukha Vadari <mvadari@gmail.com>
- Michael Legleux <legleux@users.noreply.github.com>
- Peter Chen <34582813+PeterChen13579@users.noreply.github.com>
- RichardAH <richard.holland@starstone.co.nz>
- Rome Reginelli <rome@ripple.com>
- Scott Schurr <scott@ripple.com>
- Shawn Xie <35279399+shawnxie999@users.noreply.github.com>
- drlongle <drlongle@gmail.com>

Bug Bounties and Responsible Disclosures:

We welcome reviews of the rippled code and urge researchers to responsibly disclose any issues they may find.

To report a bug, please send a detailed report to: <bugs@xrpl.org>


# Introducing XRP Ledger version 1.11.0

Version 1.11.0 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available.

This release reduces memory usage, introduces the `fixNFTokenRemint` amendment, and adds new features and bug fixes. For example, the new NetworkID field in transactions helps to prevent replay attacks with side-chains.

[Sign Up for Future Release Announcements](https://groups.google.com/g/ripple-server)

<!-- BREAK -->

## Action Required

The `fixNFTokenRemint` amendment is now open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, upgrade to version 1.11.0 by July 5 to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.


## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).


## What's Changed

### New Features and Improvements

* Allow port numbers be be specified using a either a colon or a space by @RichardAH in https://github.com/XRPLF/rippled/pull/4328
* Eliminate memory allocation from critical path: by @nbougalis in https://github.com/XRPLF/rippled/pull/4353
* Make it easy for projects to depend on libxrpl by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4449
* Add the ability to mark amendments as obsolete by @ximinez in https://github.com/XRPLF/rippled/pull/4291
* Always create the FeeSettings object in genesis ledger by @ximinez in https://github.com/XRPLF/rippled/pull/4319
* Log exception messages in several locations by @drlongle in https://github.com/XRPLF/rippled/pull/4400
* Parse flags in account_info method by @drlongle in https://github.com/XRPLF/rippled/pull/4459
* Add NFTokenPages to account_objects RPC by @RichardAH in https://github.com/XRPLF/rippled/pull/4352
* add jss fields used by clio `nft_info` by @ledhed2222 in https://github.com/XRPLF/rippled/pull/4320
* Introduce a slab-based memory allocator and optimize SHAMapItem by @nbougalis in https://github.com/XRPLF/rippled/pull/4218
* Add NetworkID field to transactions to help prevent replay attacks on and from side-chains by @RichardAH in https://github.com/XRPLF/rippled/pull/4370
* If present, set quorum based on command line. by @mtrippled in https://github.com/XRPLF/rippled/pull/4489
* API does not accept seed or public key for account by @drlongle in https://github.com/XRPLF/rippled/pull/4404
* Add `nftoken_id`, `nftoken_ids` and `offer_id` meta fields into NFT `Tx` responses by @shawnxie999 in https://github.com/XRPLF/rippled/pull/4447

### Bug Fixes

* fix(gateway_balances): handle overflow exception by @RichardAH in https://github.com/XRPLF/rippled/pull/4355
* fix(ValidatorSite): handle rare null pointer dereference in timeout by @ximinez in https://github.com/XRPLF/rippled/pull/4420
* RPC commands understand markers derived from all ledger object types by @ximinez in https://github.com/XRPLF/rippled/pull/4361
* `fixNFTokenRemint`: prevent NFT re-mint: by @shawnxie999 in https://github.com/XRPLF/rippled/pull/4406
* Fix a case where ripple::Expected returned a json array, not a value by @scottschurr in https://github.com/XRPLF/rippled/pull/4401
* fix: Ledger data returns an empty list (instead of null) when all entries are filtered out by @drlongle in https://github.com/XRPLF/rippled/pull/4398
* Fix unit test ripple.app.LedgerData by @drlongle in https://github.com/XRPLF/rippled/pull/4484
* Fix the fix for std::result_of by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4496
* Fix errors for Clang 16 by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4501
* Ensure that switchover vars are initialized before use: by @seelabs in https://github.com/XRPLF/rippled/pull/4527
* Move faulty assert by @ximinez in https://github.com/XRPLF/rippled/pull/4533
* Fix unaligned load and stores: (#4528) by @seelabs in https://github.com/XRPLF/rippled/pull/4531
* fix node size estimation by @dangell7 in https://github.com/XRPLF/rippled/pull/4536
* fix: remove redundant moves by @ckeshava in https://github.com/XRPLF/rippled/pull/4565

### Code Cleanup and Testing

* Replace compare() with the three-way comparison operator in base_uint, Issue and Book by @drlongle in https://github.com/XRPLF/rippled/pull/4411
* Rectify the import paths of boost::function_output_iterator by @ckeshava in https://github.com/XRPLF/rippled/pull/4293
* Expand Linux test matrix by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4454
* Add patched recipe for SOCI by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4510
* Switch to self-hosted runners for macOS by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4511
* [TRIVIAL] Add missing includes by @seelabs in https://github.com/XRPLF/rippled/pull/4555

### Docs

* Refactor build instructions by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4381
* Add install instructions for package managers by @thejohnfreeman in https://github.com/XRPLF/rippled/pull/4472
* Fix typo by @solmsted in https://github.com/XRPLF/rippled/pull/4508
* Update environment.md by @sappenin in https://github.com/XRPLF/rippled/pull/4498
* Update BUILD.md by @oeggert in https://github.com/XRPLF/rippled/pull/4514
* Trivial: add comments for NFToken-related invariants by @scottschurr in https://github.com/XRPLF/rippled/pull/4558

## New Contributors
* @drlongle made their first contribution in https://github.com/XRPLF/rippled/pull/4411
* @ckeshava made their first contribution in https://github.com/XRPLF/rippled/pull/4293
* @solmsted made their first contribution in https://github.com/XRPLF/rippled/pull/4508
* @sappenin made their first contribution in https://github.com/XRPLF/rippled/pull/4498
* @oeggert made their first contribution in https://github.com/XRPLF/rippled/pull/4514

**Full Changelog**: https://github.com/XRPLF/rippled/compare/1.10.1...1.11.0


### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome all contributions and invite everyone to join the community of XRP Ledger developers to help build the Internet of Value.

### Credits

The following people contributed directly to this release:
- Alloy Networks <45832257+alloynetworks@users.noreply.github.com>
- Brandon Wilson <brandon@coil.com>
- Chenna Keshava B S <21219765+ckeshava@users.noreply.github.com>
- David Fuelling <sappenin@gmail.com>
- Denis Angell <dangell@transia.co>
- Ed Hennis <ed@ripple.com>
- Elliot Lee <github.public@intelliot.com>
- John Freeman <jfreeman08@gmail.com>
- Mark Travis <mtrippled@users.noreply.github.com>
- Nik Bougalis <nikb@bougalis.net>
- RichardAH <richard.holland@starstone.co.nz>
- Scott Determan <scott.determan@yahoo.com>
- Scott Schurr <scott@ripple.com>
- Shawn Xie <35279399+shawnxie999@users.noreply.github.com>
- drlongle <drlongle@gmail.com>
- ledhed2222 <ledhed2222@users.noreply.github.com>
- oeggert <117319296+oeggert@users.noreply.github.com>
- solmsted <steven.olm@gmail.com>


Bug Bounties and Responsible Disclosures:
We welcome reviews of the rippled code and urge researchers to
responsibly disclose any issues they may find.

To report a bug, please send a detailed report to:

    bugs@xrpl.org


# Introducing XRP Ledger version 1.10.1

Version 1.10.1 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available. This release restores packages for Ubuntu 18.04.

Compared to version 1.10.0, the only C++ code change fixes an edge case in Reporting Mode.

If you are already running version 1.10.0, then upgrading to version 1.10.1 is generally not required.

[Sign Up for Future Release Announcements](https://groups.google.com/g/ripple-server)

<!-- BREAK -->

## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).

## Changelog

- [`da18c86cbf`](https://github.com/ripple/rippled/commit/da18c86cbfea1d8fe6940035f9103e15890d47ce) Build packages with Ubuntu 18.04
- [`f7b3ddd87b`](https://github.com/ripple/rippled/commit/f7b3ddd87b8ef093a06ab1420bea57ed1e77643a) Reporting Mode: Do not attempt to acquire missing data from peer network (#4458)

### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome all contributions and invite everyone to join the community of XRP Ledger developers to help build the Internet of Value.

### Credits

The following people contributed directly to this release:

- John Freeman <jfreeman08@gmail.com>
- Mark Travis <mtrippled@users.noreply.github.com>
- Michael Legleux <mlegleux@ripple.com>

Bug Bounties and Responsible Disclosures:
We welcome reviews of the rippled code and urge researchers to
responsibly disclose any issues they may find.

To report a bug, please send a detailed report to:

    bugs@xrpl.org


# Introducing XRP Ledger version 1.10.0

Version 1.10.0 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available. This release introduces six new amendments, detailed below, and cleans up code to improve performance.

[Sign Up for Future Release Announcements](https://groups.google.com/g/ripple-server)

<!-- BREAK -->

## Action Required

Six new amendments are now open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, upgrade to version 1.10.0 by March 21 to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.


## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).


## New Amendments

- **`featureImmediateOfferKilled`**: Changes the response code of an `OfferCreate` transaction with the `tfImmediateOrCancel` flag to return `tecKILLED` when no funds are moved. The previous return code of `tecSUCCESS` was unintuitive. [#4157](https://github.com/XRPLF/rippled/pull/4157)

- **`featureDisallowIncoming`**: Enables an account to block incoming checks, payment channels, NFToken offers, and trust lines. [#4336](https://github.com/XRPLF/rippled/pull/4336)

- **`featureXRPFees`**: Simplifies transaction cost calculations to use XRP directly, rather than calculating indirectly in "fee units" and translating the results to XRP. Updates all instances of "fee units" in the protocol and ledger data to be drops of XRP instead. [#4247](https://github.com/XRPLF/rippled/pull/4247) 

- **`fixUniversalNumber`**: Simplifies and unifies the code for decimal floating point math. In some cases, this provides slightly better accuracy than the previous code, resulting in calculations whose least significant digits are different than when calculated with the previous code. The different results may cause other edge case differences where precise calculations are used, such as ranking of offers or processing of payments that use several different paths. [#4192](https://github.com/XRPLF/rippled/pull/4192)

- **`fixNonFungibleTokensV1_2`**: This amendment is a combination of NFToken fixes. [#4417](https://github.com/XRPLF/rippled/pull/4417)
  - Fixes unburnable NFTokens when it has over 500 offers. [#4346](https://github.com/XRPLF/rippled/pull/4346)
  - Fixes 3 NFToken offer acceptance issues. [#4380](https://github.com/XRPLF/rippled/pull/4380)
  - Prevents brokered sales of NFTokens to owners. [#4403](https://github.com/XRPLF/rippled/pull/4403)
  - Only allows the destination to settle NFToken offers through brokerage. [#4399](https://github.com/XRPLF/rippled/pull/4399)

- **`fixTrustLinesToSelf`**: Trust lines must be between two different accounts, but two exceptions exist because of a bug that briefly existed. This amendment removes those trust lines. [69bb2be](https://github.com/XRPLF/rippled/pull/4270/commits/69bb2be446e3cc24c694c0835b48bd2ecd3d119e)


## Changelog


### New Features and Improvements

- **Improve Handshake in the peer protocol**: Switched to using a cryptographically secure PRNG for the Instance Cookie. `rippled` now uses hex encoding for the `Closed-Ledger` and `Previous-Ledger` fields in the Handshake. Also added `--newnodeid` and `--nodeid` command line options. [5a15229](https://github.com/XRPLF/rippled/pull/4270/commits/5a15229eeb13b69c8adf1f653b88a8f8b9480546)

- **RPC tooBusy response now has 503 HTTP status code**: Added ripplerpc 3.0, enabling RPC tooBusy responses to return relevant HTTP status codes. This is a non-breaking change that only applies to JSON-RPC when you include `"ripplerpc": "3.0"` in the request. [#4143](https://github.com/XRPLF/rippled/pull/4143)

- **Use the Conan package manager**: Added a `conanfile.py` and Conan recipe for Snappy. Removed the RocksDB recipe from the repo; you can now get it from Conan Center. [#4367](https://github.com/XRPLF/rippled/pull/4367), [c2b03fe](https://github.com/XRPLF/rippled/commit/c2b03fecca19a304b37467b01fa78593d3dce3fb)

- **Update Build Instructions**: Updated the build instructions to build with the Conan package manager and restructured info for easier comprehension. [#4376](https://github.com/XRPLF/rippled/pull/4376), [#4383](https://github.com/XRPLF/rippled/pull/4383)

- **Revise CONTRIBUTING**: Updated code contribution guidelines. `rippled` is an open source project and contributions are very welcome. [#4382](https://github.com/XRPLF/rippled/pull/4382)

- **Update documented pathfinding configuration defaults**: `417cfc2` changed the default Path Finding configuration values, but missed updating the values documented in rippled-example.cfg. Updated those defaults and added recommended values for nodes that want to support advanced pathfinding. [#4409](https://github.com/XRPLF/rippled/pull/4409)

- **Remove gRPC code previously used for the Xpring SDK**: Removed gRPC code used for the Xpring SDK. The gRPC API is also enabled locally by default in `rippled-example.cfg`. This API is used for [Reporting Mode](https://xrpl.org/build-run-rippled-in-reporting-mode.html) and [Clio](https://github.com/XRPLF/clio). [28f4cc7](https://github.com/XRPLF/rippled/pull/4321/commits/28f4cc7817c2e477f0d7e9ade8f07a45ff2b81f1)

- **Switch from C++17 to C++20**: Updated `rippled` to use C++20. [92d35e5](https://github.com/XRPLF/rippled/pull/4270/commits/92d35e54c7de6bbe44ff6c7c52cc0765b3f78258)

- **Support for Boost 1.80.0:**: [04ef885](https://github.com/XRPLF/rippled/pull/4321/commits/04ef8851081f6ee9176783ad3725960b8a931ebb)

- **Reduce default reserves to 10/2**: Updated the hard-coded default reserves to match the current settings on Mainnet. [#4329](https://github.com/XRPLF/rippled/pull/4329)

- **Improve self-signed certificate generation**: Improved speed and security of TLS certificate generation on fresh startup. [0ecfc7c](https://github.com/XRPLF/rippled/pull/4270/commits/0ecfc7cb1a958b731e5f184876ea89ae2d4214ee)


### Bug Fixes

- **Update command-line usage help message**: Added `manifest` and `validator_info` to the `rippled` CLI usage statement. [b88ed5a](https://github.com/XRPLF/rippled/pull/4270/commits/b88ed5a8ec2a0735031ca23dc6569d54787dc2f2)

- **Work around gdb bug by changing a template parameter**: Added a workaround for a bug in gdb, where unsigned template parameters caused issues with RTTI. [#4332](https://github.com/XRPLF/rippled/pull/4332)

- **Fix clang 15 warnings**: [#4325](https://github.com/XRPLF/rippled/pull/4325)

- **Catch transaction deserialization error in doLedgerGrpc**: Fixed an issue in the gRPC API, so `Clio` can extract ledger headers and state objects from specific transactions that can't be deserialized by `rippled` code. [#4323](https://github.com/XRPLF/rippled/pull/4323)

- **Update dependency: gRPC**: New Conan recipes broke the old version of gRPC, so the dependency was updated. [#4407](https://github.com/XRPLF/rippled/pull/4407)

- **Fix Doxygen workflow**: Added options to build documentation that don't depend on the library dependencies of `rippled`. [#4372](https://github.com/XRPLF/rippled/pull/4372)

- **Don't try to read SLE with key 0 from the ledger**: Fixed the `preclaim` function to check for 0 in `NFTokenSellOffer` and `NFTokenBuyOffer` before calling `Ledger::read`. This issue only affected debug builds. [#4351](https://github.com/XRPLF/rippled/pull/4351)

- **Update broken link to hosted Doxygen content**: [5e1cb09](https://github.com/XRPLF/rippled/pull/4270/commits/5e1cb09b8892e650f6c34a66521b6b1673bd6b65)


### Code Cleanup

- **Prevent unnecessary `shared_ptr` copies by accepting a value in `SHAMapInnerNode::setChild`**: [#4266](https://github.com/XRPLF/rippled/pull/4266)

- **Release TaggedCache object memory outside the lock**: [3726f8b](https://github.com/XRPLF/rippled/pull/4321/commits/3726f8bf31b3eab8bab39dce139656fd705ae9a0)

- **Rename SHAMapStoreImp::stopping() to healthWait()**: [7e9e910](https://github.com/XRPLF/rippled/pull/4321/commits/7e9e9104eabbf0391a0837de5630af17a788e233)

- **Improve wrapper around OpenSSL RAND**: [7b3507b](https://github.com/XRPLF/rippled/pull/4270/commits/7b3507bb873495a974db33c57a888221ddabcacc)

- **Improve AccountID string conversion caching**: Improved memory cache usage. [e2eed96](https://github.com/XRPLF/rippled/pull/4270/commits/e2eed966b0ecb6445027e6a023b48d702c5f4832)

- **Build the command map at compile time**: [9aaa0df](https://github.com/XRPLF/rippled/pull/4270/commits/9aaa0dff5fd422e5f6880df8e20a1fd5ad3b4424)

- **Avoid unnecessary copying and dynamic memory allocations**: [d318ab6](https://github.com/XRPLF/rippled/pull/4270/commits/d318ab612adc86f1fd8527a50af232f377ca89ef)

- **Use constexpr to check memo validity**: [e67f905](https://github.com/XRPLF/rippled/pull/4270/commits/e67f90588a9050162881389d7e7d1d0fb31066b0)

- **Remove charUnHex**: [83ac141](https://github.com/XRPLF/rippled/pull/4270/commits/83ac141f656b1a95b5661853951ebd95b3ffba99)

- **Remove deprecated AccountTxOld.cpp**: [ce64f7a](https://github.com/XRPLF/rippled/pull/4270/commits/ce64f7a90f99c6b5e68d3c3d913443023de061a6)

- **Remove const_cast usage**: [23ce431](https://github.com/XRPLF/rippled/pull/4321/commits/23ce4318768b718c82e01004d23f1abc9a9549ff)

- **Remove inaccessible code paths and outdated data format wchar_t**: [95fabd5](https://github.com/XRPLF/rippled/pull/4321/commits/95fabd5762a4917753c06268192e4d4e4baef8e4)

- **Improve move semantics in Expected**: [#4326](https://github.com/XRPLF/rippled/pull/4326)


### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome all contributions and invite everyone to join the community of XRP Ledger developers to help build the Internet of Value.

### Credits

The following people contributed directly to this release:

- Alexander Kremer <akremer@ripple.com>
- Alloy Networks <45832257+alloynetworks@users.noreply.github.com>
- CJ Cobb <46455409+cjcobb23@users.noreply.github.com>
- Chenna Keshava B S <ckbs.keshava56@gmail.com>
- Crypto Brad Garlinghouse <cryptobradgarlinghouse@protonmail.com>
- Denis Angell <dangell@transia.co>
- Ed Hennis <ed@ripple.com>
- Elliot Lee <github.public@intelliot.com>
- Gregory Popovitch <greg7mdp@gmail.com>
- Howard Hinnant <howard.hinnant@gmail.com>
- J. Scott Branson <18340247+crypticrabbit@users.noreply.github.com>
- John Freeman <jfreeman08@gmail.com>
- ledhed2222 <ledhed2222@users.noreply.github.com>
- Levin Winter <33220502+levinwinter@users.noreply.github.com>
- manojsdoshi <mdoshi@ripple.com>
- Nik Bougalis <nikb@bougalis.net>
- RichardAH <richard.holland@starstone.co.nz>
- Scott Determan <scott.determan@yahoo.com>
- Scott Schurr <scott@ripple.com>
- Shawn Xie <35279399+shawnxie999@users.noreply.github.com>

Security Bug Bounty Acknowledgements:
- Aaron Hook
- Levin Winter

Bug Bounties and Responsible Disclosures:
We welcome reviews of the rippled code and urge researchers to
responsibly disclose any issues they may find.

To report a bug, please send a detailed report to:

    bugs@xrpl.org


# Introducing XRP Ledger version 1.9.4

Version 1.9.4 of `rippled`, the reference implementation of the XRP Ledger protocol is now available. This release introduces an amendment that removes the ability for an NFT issuer to indicate that trust lines should be automatically created for royalty payments from secondary sales of NFTs, in response to a bug report that indicated how this functionality could be abused to mount a denial of service attack against the issuer.

## Action Required

This release introduces a new amendment to the XRP Ledger protocol, **`fixRemoveNFTokenAutoTrustLine`** to mitigate a potential denial-of-service attack against NFT issuers that minted NFTs and allowed secondary trading of those NFTs to create trust lines for any asset.

This amendment is open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, then you should upgrade to version 1.9.4 within two weeks, to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.

For more information about NFTs on the XRP Ledger, see [NFT Conceptual Overview](https://xrpl.org/nft-conceptual-overview.html).


## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).

## Changelog

## Contributions

The primary change in this release is the following bug fix:

- **Introduce fixRemoveNFTokenAutoTrustLine amendment**: Introduces the `fixRemoveNFTokenAutoTrustLine` amendment, which disables the `tfTrustLine` flag, which a malicious attacker could exploit to mount denial-of-service attacks against NFT issuers that specified the flag on their NFTs. ([#4301](https://github.com/XRPLF/rippled/4301))


### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome all contributions and invite everyone to join the community of XRP Ledger developers and help us build the Internet of Value.

### Credits

The following people contributed directly to this release:

- Scott Schurr <scott@ripple.com>
- Howard Hinnant <howard@ripple.com>
- Scott Determan <scott.determan@yahoo.com>
- Ikko Ashimine <eltociear@gmail.com>


# Introducing XRP Ledger version 1.9.3

Version 1.9.3 of `rippled`, the reference server implementation of the XRP Ledger protocol is now available. This release corrects minor technical flaws with the code that loads configured amendment votes after a startup and the copy constructor of `PublicKey`.

## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).

## Changelog

## Contributions

This release contains the following bug fixes:

- **Change by-value to by-reference to persist vote**: A minor technical flaw, caused by use of a copy instead of a reference, resulted in operator-configured "yes" votes to not be properly loaded after a restart. ([#4256](https://github.com/XRPLF/rippled/pull/4256))
- **Properly handle self-assignment of PublicKey**: The `PublicKey` copy assignment operator mishandled the case where a `PublicKey` would be assigned to itself, and could result in undefined behavior. 

### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome contributions, big and small, and invite everyone to join the community of XRP Ledger developers and help us build the Internet of Value.

### Credits

The following people contributed directly to this release:

- Howard Hinnant <howard@ripple.com>
- Crypto Brad Garlinghouse <cryptobradgarlinghouse@protonmail.com>
- Wo Jake <87929946+wojake@users.noreply.github.com>


# Introducing XRP Ledger version 1.9.2

Version 1.9.2 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available. This release includes several fixes and improvements, including a second new fix amendment to correct a bug in Non-Fungible Tokens (NFTs) code, a new API method for order book changes, less noisy logging, and other small fixes.

<!-- BREAK -->


## Action Required

This release introduces a two new amendments to the XRP Ledger protocol. The first, **fixNFTokenNegOffer**, fixes a bug in code associated with the **NonFungibleTokensV1** amendment, originally introduced in [version 1.9.0](https://xrpl.org/blog/2022/rippled-1.9.0.html). The second, **NonFungibleTokensV1_1**, is a "roll-up" amendment that enables the **NonFungibleTokensV1** feature plus the two fix amendments associated with it, **fixNFTokenDirV1** and **fixNFTokenNegOffer**.

If you want to enable NFT code on the XRP Ledger Mainnet, you can vote in favor of only the **NonFungibleTokensV1_1** amendment to support enabling the feature and fixes together, without risk that the unfixed NFT code may become enabled first.

These amendments are now open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, then you should upgrade to version 1.9.2 within two weeks, to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.

For more information about NFTs on the XRP Ledger, see [NFT Conceptual Overview](https://xrpl.org/nft-conceptual-overview.html).

## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).

## Changelog

This release contains the following features and improvements.

- **Introduce fixNFTokenNegOffer amendment.** This amendment fixes a bug in the Non-Fungible Tokens (NFTs) functionality provided by the NonFungibleTokensV1 amendment (not currently enabled on Mainnet). The bug allowed users to place offers to buy tokens for negative amounts of money when using Brokered Mode. Anyone who accepted such an offer would transfer the token _and_ pay money. This amendment explicitly disallows offers to buy or sell NFTs for negative amounts of money, and returns an appropriate error code. This also corrects the error code returned when placing offers to buy or sell NFTs for negative amounts in Direct Mode. ([8266d9d](https://github.com/XRPLF/rippled/commit/8266d9d598d19f05e1155956b30ca443c27e119e))
- **Introduce `NonFungibleTokensV1_1` amendment.** This amendment encompasses three NFT-related amendments: the original NonFungibleTokensV1 amendment (from version 1.9.0), the fixNFTokenDirV1 amendment (from version 1.9.1), and the new fixNFTokenNegOffer amendment from this release. This amendment contains no changes other than enabling those three amendments together; this allows validators to vote in favor of _only_ enabling the feature and fixes at the same time. ([59326bb](https://github.com/XRPLF/rippled/commit/59326bbbc552287e44b3a0d7b8afbb1ddddb3e3b))
- **Handle invalid port numbers.** If the user specifies a URL with an invalid port number, the server would silently attempt to use port 0 instead. Now it raises an error instead. This affects admin API methods and config file parameters for downloading history shards and specifying validator list sites. ([#4213](https://github.com/XRPLF/rippled/pull/4213))
- **Reduce log noisiness.** Decreased the severity of benign log messages in several places: "addPathsForType" messages during regular operation, expected errors during unit tests, and missing optional documentation components when compiling from source. ([#4178](https://github.com/XRPLF/rippled/pull/4178), [#4166](https://github.com/XRPLF/rippled/pull/4166), [#4180](https://github.com/XRPLF/rippled/pull/4180))
- **Fix race condition in history shard implementation and support clang's ThreadSafetyAnalysis tool.**  Added build settings so that developers can use this feature of the clang compiler to analyze the code for correctness, and fix an error found by this tool, which was the source of rare crashes in unit tests. ([#4188](https://github.com/XRPLF/rippled/pull/4188))
- **Prevent crash when rotating a database with missing data.** When rotating databases, a missing entry could cause the server to crash. While there should never be a missing database entry, this change keeps the server running by aborting database rotation. ([#4182](https://github.com/XRPLF/rippled/pull/4182))
- **Fix bitwise comparison in OfferCreate.** Fixed an expression that incorrectly used a bitwise comparison for two boolean values rather than a true boolean comparison. The outcome of the two comparisons is equivalent, so this is not a transaction processing change, but the bitwise comparison relied on compilers to implicitly fix the expression. ([#4183](https://github.com/XRPLF/rippled/pull/4183))
- **Disable cluster timer when not in a cluster.** Disabled a timer that was unused on servers not running in clustered mode. The functionality of clustered servers is unchanged. ([#4173](https://github.com/XRPLF/rippled/pull/4173))
- **Limit how often to process peer discovery messages.** In the peer-to-peer network, servers periodically share IP addresses of their peers with each other to facilitate peer discovery. It is not necessary to process these types of messages too often; previously, the code tracked whether it needed to process new messages of this type but always processed them anyway. With this change, the server no longer processes peer discovery messages if it has done so recently. ([#4202](https://github.com/XRPLF/rippled/pull/4202))
- **Improve STVector256 deserialization.** Optimized the processing of this data type in protocol messages. This data type is used in several types of ledger entry that are important for bookkeeping, including directory pages that track other ledger types, amendments tracking, and the ledger hashes history. ([#4204](https://github.com/XRPLF/rippled/pull/4204))
- **Fix and refactor spinlock code.** The spinlock code, which protects the `SHAMapInnerNode` child lists, had a mistake that allowed the same child to be repeatedly locked under some circumstances. Fixed this bug and improved the spinlock code to make it easier to use correctly and easier to verify that the code works correctly. ([#4201](https://github.com/XRPLF/rippled/pull/4201))
- **Improve comments and contributor documentation.** Various minor documentation changes including some to reflect the fact that the source code repository is now owned by the XRP Ledger Foundation. ([#4214](https://github.com/XRPLF/rippled/pull/4214), [#4179](https://github.com/XRPLF/rippled/pull/4179), [#4222](https://github.com/XRPLF/rippled/pull/4222))
- **Introduces a new API book_changes to provide information in a format that is useful for building charts that highlight DEX activity at a per-ledger level.**  ([#4212](https://github.com/XRPLF/rippled/pull/4212))

## Contributions

### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/XRPLF/rippled>.

We welcome contributions, big and small, and invite everyone to join the community of XRP Ledger developers and help us build the Internet of Value.

### Credits

The following people contributed directly to this release:

- Chenna Keshava B S <ckbs.keshava56@gmail.com>
- Ed Hennis <ed@ripple.com>
- Ikko Ashimine <eltociear@gmail.com>
- Nik Bougalis <nikb@bougalis.net>
- Richard Holland <richard.holland@starstone.co.nz>
- Scott Schurr <scott@ripple.com>
- Scott Determan <scott.determan@yahoo.com>

For a real-time view of all lifetime contributors, including links to the commits made by each, please visit the "Contributors" section of the GitHub repository: <https://github.com/XRPLF/rippled/graphs/contributors>.

# Introducing XRP Ledger version 1.9.1

Version 1.9.1 of `rippled`, the reference server implementation of the XRP Ledger protocol, is now available. This release includes several important fixes, including a fix for a syncing issue from 1.9.0, a new fix amendment to correct a bug in the new Non-Fungible Tokens (NFTs) code, and a new amendment to allow multi-signing by up to 32 signers.

<!-- BREAK -->


## Action Required

This release introduces two new amendments to the XRP Ledger protocol. These amendments are now open for voting according to the XRP Ledger's [amendment process](https://xrpl.org/amendments.html), which enables protocol changes following two weeks of >80% support from trusted validators.

If you operate an XRP Ledger server, then you should upgrade to version 1.9.1 within two weeks, to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.

The **fixNFTokenDirV1** amendment fixes a bug in code associated with the **NonFungibleTokensV1** amendment, so the fixNFTokenDirV1 amendment should be enabled first. All validator operators are encouraged to [configure amendment voting](https://xrpl.org/configure-amendment-voting.html) to oppose the NonFungibleTokensV1 amendment until _after_ the fixNFTokenDirV1 amendment has become enabled. For more information about NFTs on the XRP Ledger, see [NFT Conceptual Overview](https://xrpl.org/nft-conceptual-overview.html).

The **ExpandedSignerList** amendment extends the ledger's built-in multi-signing functionality so that each list can contain up to 32 entries instead of the current limit of 8. Additionally, this amendment allows each signer to have an arbitrary 256-bit data field associated with it. This data can be used to identify the signer or provide other metadata that is useful for organizations, smart contracts, or other purposes.

## Install / Upgrade

On supported platforms, see the [instructions on installing or updating `rippled`](https://xrpl.org/install-rippled.html).

## Changelog

This release contains the following features and improvements.

## New Features and Amendments

- **Introduce fixNFTokenDirV1 Amendment** - This amendment fixes an off-by-one error that occurred in some corner cases when determining which `NFTokenPage` an `NFToken` object belongs on. It also adjusts the constraints of `NFTokenPage` invariant checks, so that certain error cases fail with a suitable error code such as `tecNO_SUITABLE_TOKEN_PAGE` instead of failing with a `tecINVARIANT_FAILED` error code. ([#4155](https://github.com/ripple/rippled/pull/4155))

- **Introduce ExpandedSignerList Amendment** - This amendment expands the maximum signer list size to 32 entries and allows each signer to have an optional 256-bit `WalletLocator` field containing arbitrary data. ([#4097](https://github.com/ripple/rippled/pull/4097))

- **Pause online deletion rather than canceling it if the server fails health check** - The server stops performing online deletion of old ledger history if the server fails its internal health check during this time. Online deletion can now resume after the server recovers, rather than having to start over. ([#4139](https://github.com/ripple/rippled/pull/4139))


## Bug Fixes and Performance Improvements

- **Fix performance issues introduced in 1.9.0** - Readjusts some parameters of the ledger acquisition engine to revert some changes introduced in 1.9.0 that had adverse effects on some systems, including causing some systems to fail to sync to the network. ([#4152](https://github.com/ripple/rippled/pull/4152))

- **Improve Memory Efficiency of Path Finding** - Finding paths for cross-currency payments is a resource-intensive operation. While that remains true, this fix improves memory usage of pathfinding by discarding trust line results that cannot be used before those results are fully loaded or cached. ([#4111](https://github.com/ripple/rippled/pull/4111))

- **Fix incorrect CMake behavior on Windows when platform is unspecified or x64** - Fixes handling of platform selection when using the cmake-gui tool to build on Windows. The generator expects `Win64` but the GUI only provides `x64` as an option, which raises an error. This fix only raises an error if the platform is `Win32` instead, allowing the generation of solution files to succeed. ([#4150](https://github.com/ripple/rippled/pull/4150))

- **Fix test failures with newer MSVC compilers on Windows** - Fixes some cases where the API handler code used string pointer comparisons, which may not work correctly with some versions of the MSVC compiler. ([#4149](https://github.com/ripple/rippled/pull/4149))

- **Update minimum Boost version to 1.71.0** - This release is compatible with Boost library versions 1.71.0 through 1.77.0. The build configuration and documentation have been updated to reflect this. ([#4134](https://github.com/ripple/rippled/pull/4134))

- **Fix unit test failures for DatabaseDownloader** - Increases a timeout in the `DatabaseDownloader` code and adjusts unit tests so that the code does not return spurious failures, and more data is logged if it does fail. ([#4021](https://github.com/ripple/rippled/pull/4021))

- **Refactor relational database interface** - Improves code comments, naming, and organization of the module that interfaces with relational databases (such as the SQLite database used for tracking transaction history). ([#3965](https://github.com/ripple/rippled/pull/3965))


## Contributions

### GitHub

The public source code repository for `rippled` is hosted on GitHub at <https://github.com/ripple/rippled>.

We welcome contributions, big and small, and invite everyone to join the community of XRP Ledger developers and help us build the Internet of Value.


### Credits

The following people contributed directly to this release:

- Devon White <dwhite@ripple.com>
- Ed Hennis <ed@ripple.com>
- Gregory Popovitch <greg7mdp@gmail.com>
- Mark Travis <mtravis@ripple.com>
- Manoj Doshi <mdoshi@ripple.com>
- Nik Bougalis <nikb@bougalis.net>
- Richard Holland <richard.holland@starstone.co.nz>
- Scott Schurr <scott@ripple.com>

For a real-time view of all lifetime contributors, including links to the commits made by each, please visit the "Contributors" section of the GitHub repository: <https://github.com/ripple/rippled/graphs/contributors>.

We welcome external contributions and are excited to see the broader XRP Ledger community continue to grow and thrive.


# Change log

- API version 2 will now return `signer_lists` in the root of the `account_info` response, no longer nested under `account_data`.

# Releases

## Version 1.9.0
This is the 1.9.0 release of `rippled`, the reference implementation of the XRP Ledger protocol. This release brings several features and improvements.

### New and Improved Features
- **Introduce NFT support (XLS020):** This release introduces support for non-fungible tokens, currently available to the developer community for broader review and testing.  Developers can create applications that allow users to mint, transfer, and ultimately burn (if desired) NFTs on the XRP Ledger. You can try out the new NFT transactions using the [nft-devnet](https://xrpl.org/xrp-testnet-faucet.html). Note that some fields and error codes from earlier releases of the supporting code have been refactored for this release, shown in the Code Refactoring section, below. [70779f](https://github.com/ripple/rippled/commit/70779f6850b5f33cdbb9cf4129bc1c259af0013e)

- **Simplify the Job Queue:** This is a refactor aimed at cleaning up and simplifying the existing job queue. Currently, all jobs are canceled at the same time and in the same way, so this commit removes the unnecessary per-job cancellation token. [#3656](https://github.com/ripple/rippled/pull/3656)

- **Optimize trust line caching:** The existing trust line caching code was suboptimal in that it stored redundant information, pinned SLEs into memory, and required multiple memory allocations per cached object. This commit eliminates redundant data, reduces the size of cached objects and unpinning SLEs from memory, and uses value types to avoid the need for `std::shared_ptr`. As a result of these changes, the effective size of a cached object includes the overhead of the memory allocator, and the `std::shared_ptr` should be reduced by at least 64 bytes. This is significant, as there can easily be tens of millions of these objects. [4d5459](https://github.com/ripple/rippled/commit/4d5459d041da8f5a349c5f458d664e5865e1f1b5)

- **Incremental improvements to pathfinding memory usage:** This commit aborts background pathfinding when closed or disconnected, exits the pathfinding job thread if there are no requests left, does not create the path find a job if there are no requests, and refactors to remove the circular dependency between InfoSub and PathRequest. [#4111](https://github.com/ripple/rippled/pull/4111)

- **Improve deterministic transaction sorting in TxQ:** This commit ensures that transactions with the same fee level are sorted by TxID XORed with the parent ledger hash, the TxQ is re-sorted after every ledger, and attempts to future-proof the TxQ tie-breaking test. [#4077](https://github.com/ripple/rippled/pull/4077)

- **Improve stop signaling for Application:** [34ca45](https://github.com/ripple/rippled/commit/34ca45713244d0defc39549dd43821784b2a5c1d)

- **Eliminate SHAMapInnerNode lock contention:** The `SHAMapInnerNode` class had a global mutex to protect the array of node children. Profiling suggested that around 4% of all attempts to lock the global would block. This commit removes that global mutex, and replaces it with a new per-node 16-way spinlock (implemented so as not to affect the size of an inner node object), effectively eliminating the lock contention. [1b9387](https://github.com/ripple/rippled/commit/1b9387eddc1f52165d3243d2ace9be0c62495eea)

- **Improve ledger-fetching logic:** When fetching ledgers, the existing code would isolate the peer that sent the most useful responses, and issue follow-up queries only to that peer. This commit increases the query aggressiveness, and changes the mechanism used to select which peers to issue follow-up queries to so as to more evenly spread the load among those peers that provided useful responses. [48803a](https://github.com/ripple/rippled/commit/48803a48afc3bede55d71618c2ee38fd9dbfd3b0)

- **Simplify and improve order book tracking:** The order book tracking code would use `std::shared_ptr` to track the lifetime of objects. This commit changes the logic to eliminate the overhead of `std::shared_ptr` by using value types, resulting in significant memory savings. [b9903b](https://github.com/ripple/rippled/commit/b9903bbcc483a384decf8d2665f559d123baaba2)

- **Negative cache support for node store:** This commit allows the cache to service requests for nodes that were previously looked up but not found, reducing the need to perform I/O in several common scenarios. [3eb8aa](https://github.com/ripple/rippled/commit/3eb8aa8b80bd818f04c99cee2cfc243192709667)

- **Improve asynchronous database handlers:** This commit optimizes the way asynchronous node store operations are processed, both by reducing the number of times locks are held and by minimizing the number of memory allocations and data copying. [6faaa9](https://github.com/ripple/rippled/commit/6faaa91850d6b2eb9fbf16c1256bf7ef11ac4646)

- **Cleanup AcceptedLedger and AcceptedLedgerTx:** This commit modernizes the `AcceptedLedger` and `AcceptedLedgerTx` classes, reduces their memory footprint, and reduces unnecessary dynamic memory allocations. [8f5868](https://github.com/ripple/rippled/commit/8f586870917818133924bf2e11acab5321c2b588)

### Code Refactoring

This release includes name changes in the NFToken API for SFields, RPC return labels, and error codes for clarity and consistency. To refactor your code, migrate the names of these items to the new names as listed below.
 
#### `SField` name changes:
* `TokenTaxon -> NFTokenTaxon`
* `MintedTokens -> MintedNFTokens`
* `BurnedTokens -> BurnedNFTokens`
* `TokenID -> NFTokenID`
* `TokenOffers -> NFTokenOffers`
* `BrokerFee -> NFTokenBrokerFee`
* `Minter -> NFTokenMinter`
* `NonFungibleToken -> NFToken`
* `NonFungibleTokens -> NFTokens`
* `BuyOffer -> NFTokenBuyOffer`
* `SellOffer -> NFTokenSellOffer`
* `OfferNode -> NFTokenOfferNode`
 
#### RPC return labels
* `tokenid -> nft_id`
* `index -> nft_offer_index`
 
#### Error codes
* `temBAD_TRANSFER_FEE -> temBAD_NFTOKEN_TRANSFER_FEE`
* `tefTOKEN_IS_NOT_TRANSFERABLE -> tefNFTOKEN_IS_NOT_TRANSFERABLE`
* `tecNO_SUITABLE_PAGE -> tecNO_SUITABLE_NFTOKEN_PAGE`
* `tecBUY_SELL_MISMATCH -> tecNFTOKEN_BUY_SELL_MISMATCH`
* `tecOFFER_TYPE_MISMATCH -> tecNFTOKEN_OFFER_TYPE_MISMATCH`
* `tecCANT_ACCEPT_OWN_OFFER -> tecCANT_ACCEPT_OWN_NFTOKEN_OFFER`


### Bug Fixes
- **Fix deletion of orphan node store directories:** Orphaned node store directories should only be deleted if the proper node store directories are confirmed to exist. [06e87e](https://github.com/ripple/rippled/commit/06e87e0f6add5b880d647e14ab3d950decfcf416)

## Version 1.8.5
This is the 1.8.5 release of `rippled`, the reference implementation of the XRP Ledger protocol. This release includes fixes and updates for stability and security, and improvements to build scripts. There are no user-facing API or protocol changes in this release.

### Bug Fixes

This release contains the following bug fixes and under-the-hood improvements:

- **Correct TaggedPointer move constructor:** Fixes a bug in unused code for the TaggedPointer class. The old code would fail if a caller explicitly tried to remove a child that is not actually part of the node. (227a12d)

- **Ensure protocol buffer prerequisites are present:** The build scripts and packages now properly handle Protobuf packages and various packages. Prior to this change, building on Ubuntu 21.10 Impish Indri would fail unless the `libprotoc-dev` package was installed. (e06465f)

- **Improve handling of endpoints during peer discovery.** This hardens and improves handling of incoming messages on the peer protocol. (289bc0a)

- **Run tests on updated linux distros:** Test builds now run on Rocky Linux 8, Fedora 34 and 35, Ubuntu 18, 20, and 22, and Debian 9, 10, and 11. (a9ee802)

- **Avoid dereferencing empty optional in ReportingETL:** Fixes a bug in Reporting Mode that could dereference an empty optional value when throwing an error. (cdc215d)

- **Correctly add GIT_COMMIT_HASH into version string:** When building the server from a non-tagged release, the build files now add the commit ID in a way that follows the semantic-versioning standard, and correctly handle the case where the commit hash ID cannot be retrieved. (d23d37f)

- **Update RocksDB to version 6.27.3:** Updates the version of RocksDB included in the server from 6.7.3 (which was released on 2020-03-18) to 6.27.3 (released 2021-12-10).



## Version 1.8.4
This is the 1.8.4 release of `rippled`, the reference implementation of the XRP Ledger protocol.

This release corrects a technical flaw introduced with 1.8.3 that may result in failures if the newly-introduced 'fast loading' is enabled. The release also adjusts default parameters used to configure the pathfinding engine to reduce resource usage.

### Bug Fixes
- **Adjust mutex scope in `walkMapParallel`**: This commit corrects a technical flaw introduced with commit [7c12f0135897361398917ad2c8cda888249d42ae] that would result in undefined behavior if the server operator configured their server to use the 'fast loading' mechanism introduced with 1.8.3.

- **Adjust pathfinding configuration defaults**: This commit adjusts the default configuration of the pathfinding engine, to account for the size of the XRP Ledger mainnet. Unless explicitly overriden, the changes mean that pathfinding operations will return fewer, shallower paths than previous releases.


## Version 1.8.3
This is the 1.8.3 release of `rippled`, the reference implementation of the XRP Ledger protocol.

This release implements changes that improve the syncing performance of peers on the network, adds countermeasures to several routines involving LZ4 to defend against CVE-2021-3520, corrects a minor technical flaw that would result in the server not using a cache for nodestore operations, and adjusts tunable values to optimize disk I/O.

### Summary of Issues
Recently, servers in the XRP Ledger network have been taking an increasingly long time to sync back to the network after restartiningg. This is one of several releases which will be made to improve on this issue. 


### Bug Fixes

- **Parallel ledger loader & I/O performance improvements**: This commit makes several changes that, together, should decrease the time needed for a server to sync to the network. To make full use of this change, `rippled` needs to be using storage with high IOPS and operators need to explicitly enable this behavior by adding the following to their config file, under the `[node_db]` stanza:

    [node_db]
    ...
    fast_load=1

Note that when 'fast loading' is enabled the server will not open RPC and WebSocket interfaces until after the initial load is completed. Because of this, it may appear unresponsive or down.

- **Detect CVE-2021-3520 when decompressing using LZ4**: This commit adds code to detect LZ4 payloads that may result in out-of-bounds memory accesses.

- **Provide sensible default values for nodestore cache:**: The nodestore includes a built-in cache to reduce the disk I/O load but, by default, this cache was not initialized unless it was explicitly configured by the server operator. This commit introduces sensible defaults based on the server's configured node size. 

- **Adjust the number of concurrent ledger data jobs**: Processing a large amount of data at once can effectively bottleneck a server's I/O subsystem. This commits helps optimize I/O performance by controlling how many jobs can concurrently process ledger data.

- **Two small SHAMapSync improvements**: This commit makes minor changes to optimize the way memory is used and control the amount of background I/O performed when attempting to fetch missing `SHAMap` nodes.

## Version 1.8.2
Ripple has released version 1.8.2 of rippled, the reference server implementation of the XRP Ledger protocol.  This release addresses the full transaction queues and elevated transaction fees issue observed on the XRP ledger, and also provides some optimizations and small fixes to improve the server's performance overall.

### Summary of Issues
Recently, servers in the XRP Ledger network have had full transaction queues and transactions paying low fees have mostly not been able to be confirmed through the queue. After investigation, it was discovered that a large influx of transactions to the network caused it to raise the transaction costs to be proposed in the next ledger block, and defer transactions paying lower costs to later ledgers. The first part worked as designed, but deferred transactions were not being confirmed as the ledger had capacity to process them.

The root cause was that there were very many low-cost transactions that different servers in the network received in a different order due to incidental differences in timing or network topology, which caused validators to propose different sets of low-cost transactions from the queue. Since none of these transactions had support from a majority of validators, they were removed from the proposed transaction set. Normally, any transactions removed from a proposed transaction set are supposed to be retried in the next ledger, but servers attempted to put these deferred transactions into their transaction queues first, which had filled up. As a result, the deferred transactions were discarded, and the network was only able to confirm transactions that paid high costs.

### Bug Fixes

- **Address elevated transaction fees**: This change addresses the full queue problems in two ways. First, it puts deferred transactions directly into the open ledger, rather than transaction queue. This reverts a subset of the changes from [ximinez@62127d7](https://github.com/ximinez/rippled/commit/62127d725d801641bfaa61dee7d88c95e48820c5). A transaction that is in the open ledger but doesn't get validated should stay in the open ledger so that it can be proposed again right away. Second, it changes the order in which transactions are pulled from the transaction queue to increase the overlap in servers' initial transaction consensus proposals. Like the old rules, transactions paying higher fee levels are selected first. Unlike the old rules, transactions paying the same fee level are ordered by transaction ID / hash ascending. (Previously, transactions paying the same fee level were unsorted, resulting in each server having a different order.)

- **Add ignore_default option to account_lines API**: This flag, if present, suppresses the output of incoming trust lines in the default state. This is primarily motivated by observing that users often have many unwanted incoming trust lines in a default state, which are not useful in the vast majority of cases. Being able to suppress those when doing `account_lines` saves bandwidth and resources. ([#3980](https://github.com/ripple/rippled/pull/3980))

- **Make I/O and prefetch worker threads configurable**: This commit adds the ability to specify **io_workers** and **prefetch_workers** in the config file which can be used to specify the number of threads for processing raw inbound and outbound IO and configure the number of threads for performing node store prefetching. ([#3994](https://github.com/ripple/rippled/pull/3994))

- **Enforce account RPC limits by objects traversed**: This changes the way the account_objects API method counts and limits the number of objects it returns. Instead of limiting results by the number of objects found, it counts by the number of objects traversed. Additionally, the default and maximum limits for non-admin connections have been decreased. This reduces the amount of work that one API call can do so that public API servers can share load more effectively. ([#4032](https://github.com/ripple/rippled/pull/4032))

- **Fix a crash on shutdown**: The NuDB backend class could throw an error in its destructor, resulting in a crash while the server was shutting down gracefully. This crash was harmless but resulted in false alarms and noise when tracking down other possible crashes. ([#4017](https://github.com/ripple/rippled/pull/4017))

- **Improve reporting of job queue in admin server_info**: The server_info command, when run with admin permissions, provides information about jobs in the server's job queue. This commit provides more descriptive names and more granular categories for many jobs that were previously all identified as "clientCommand". ([#4031](https://github.com/ripple/rippled/pull/4031))

- **Improve full & compressed inner node deserialization**: Remove a redundant copy operation from low-level SHAMap deserialization. ([#4004](https://github.com/ripple/rippled/pull/4004))

- **Reporting mode: only forward to P2P nodes that are synced**: Previously, reporting mode servers forwarded to any of their configured P2P nodes at random. This commit improves the selection so that it only chooses from P2P nodes that are fully synced with the network. ([#4028](https://github.com/ripple/rippled/pull/4028))

- **Improve handling of HTTP X-Forwarded-For and Forwarded headers**: Fixes the way the server handles IPv6 addresses in these HTTP headers. ([#4009](https://github.com/ripple/rippled/pull/4009), [#4030](https://github.com/ripple/rippled/pull/4030))

- **Other minor improvements to logging and Reporting Mode.**


## Version 1.8.0
Ripple has released version 1.8.0 of rippled, the reference server implementation of the XRP Ledger protocol. This release brings several features and improvements.

### New and Improved Features

- **Improve History Sharding**: Shards of ledger history are now assembled in a deterministic way so that any server can make a binary-identical shard for a given range of ledgers. This makes it possible to retrieve a shard from multiple sources in parallel, then verify its integrity by comparing checksums with peers' checksums for the same shard. Additionally, there's a new admin RPC command to import ledger history from the shard store, and the crawl_shards command has been expanded with more info. ([#2688](https://github.com/ripple/rippled/issues/2688), [#3726](https://github.com/ripple/rippled/pull/3726), [#3875](https://github.com/ripple/rippled/pull/3875))
- **New CheckCashMakesTrustLine Amendment**: If enabled, this amendment will change the CheckCash transaction type so that cashing a check for an issued token automatically creates a trust line to hold the token, similar to how purchasing a token in the decentralized exchange creates a trust line to hold the token. This change provides a way for issuers to send tokens to a user before that user has set up a trust line, but without forcing anyone to hold tokens they don't want. ([#3823](https://github.com/ripple/rippled/pull/3823))
- **Automatically determine the node size**: The server now selects an appropriate `[node_size]` configuration value by default if it is not explicitly specified. This parameter tunes various settings to the specs of the hardware that the server is running on, especially the amount of RAM and the number of CPU threads available in the system. Previously the server always chose the smallest value by default.
- **Improve transaction relaying logic**: Previously, the server relayed every transaction to all its peers (except the one that it received the transaction from). To reduce redundant messages, the server now relays transactions to a subset of peers using a randomized algorithm. Peers can determine whether there are transactions they have not seen and can request them from a peer that has them. It is expected that this feature will further reduce the bandwidth needed to operate a server.
- **Improve the Byzantine validator detector**: This expands the detection capabilities of the Byzantine validation detector. Previously, the server only monitored validators on its own UNL. Now, the server monitors for Byzantine behavior in all validations it sees.
- **Experimental tx stream with history for sidechains**: Adds an experimental subscription stream for sidechain federators to track messages on the main chain in canonical order. This stream is expected to change or be replaced in future versions as work on sidechains matures.
- **Support Debian 11 Bullseye**: This is the first release that is compatible with Debian Linux version 11.x, "Bullseye." The .deb packages now use absolute paths only, for compatibility with Bullseye's stricter package requirements. ([#3909](https://github.com/ripple/rippled/pull/3909))
- **Improve Cache Performance**: The server uses a new storage structure for several in-memory caches for greatly improved overall performance. The process of purging old data from these caches, called "sweeping", was time-consuming and blocked other important activities necessary for maintaining ledger state and participating in consensus. The new structure divides the caches into smaller partitions that can be swept in parallel.
- **Amendment default votes:** Introduces variable default votes per amendment. Previously the server always voted "yes" on any new amendment unless an admin explicitly configured a voting preference for that amendment. Now the server's default vote can be "yes" or "no" in the source code. This should allow a safer, more gradual roll-out of new amendments, as new releases can be configured to understand a new amendment but not vote for it by default. ([#3877](https://github.com/ripple/rippled/pull/3877))
- **More fields in the `validations` stream:** The `validations` subscription stream in the API now reports additional fields that were added to validation messages by the HardenedValidations amendment. These fields make it easier to detect misconfigurations such as multiple servers sharing a validation key pair. ([#3865](https://github.com/ripple/rippled/pull/3865))
- **Reporting mode supports `validations` and `manifests` streams:** In the API it is now possible to connect to these streams when connected to a servers running in reporting. Previously, attempting to subscribe to these streams on a reporting server failed with the error `reportingUnsupported`. ([#3905](https://github.com/ripple/rippled/pull/3905))

### Bug Fixes

- **Clarify the safety of NetClock::time_point arithmetic**: * NetClock::rep is uint32_t and can be error-prone when   used with subtraction. * Fixes [#3656](https://github.com/ripple/rippled/pull/3656)
- **Fix out-of-bounds reserve, and some minor optimizations**
- **Fix nested locks in ValidatorSite**
- **Fix clang warnings about copies vs references**
- **Fix reporting mode build issue**
- **Fix potential deadlock in Validator sites**
- **Use libsecp256k1 instead of OpenSSL for key derivation**: The deterministic key derivation code was still using calls to OpenSSL. This replaces the OpenSSL-based routines with new libsecp256k1-based implementations
- **Improve NodeStore to ShardStore imports**: This runs the import process in a background thread while preventing online_delete from removing ledgers pending import
- **Simplify SHAMapItem construction**: The existing class offered several constructors which were mostly unnecessary. This eliminates all existing constructors and introduces a single new one, taking a `Slice`. The internal buffer is switched from `std::vector` to `Buffer` to save a minimum of 8 bytes (plus the buffer slack that is inherent in `std::vector`) per SHAMapItem instance.
- **Redesign stoppable objects**: Stoppable is no longer an abstract base class, but a pattern, modeled after the well-understood `std::thread`. The immediate benefits are less code, less synchronization, less runtime work, and (subjectively) more readable code. The end goal is to adhere to RAII in our object design, and this is one necessary step on that path.

## Version 1.7.3

This is the 1.7.3 release of `rippled`, the reference implementation of the XRP Ledger protocol. This release addresses an OOB memory read identified by Guido Vranken, as well as an unrelated issue identified by the Ripple C++ team that could result in incorrect use of SLEs. Additionally, this version also introduces the `NegativeUNL` amendment, which corresponds to the feature which was introduced with the 1.6.0 release.

## Action Required

If you operate an XRP Ledger server, then you should upgrade to version 1.7.3 at your earliest convenience to mitigate the issues addressed in this hotfix. If a sufficient majority of servers on the network upgrade, the `NegativeUNL` amendment may gain a majority, at which point a two week activation countdown will begin. If the `NegativeUNL` amendment activates, servers running versions of `rippled` prior to 1.7.3 will become [amendment blocked](https://xrpl.org/amendments.html#amendment-blocked).

### Bug Fixes

- **Improve SLE usage in check cashing**: Fixes a situation which could result in the incorrect use of SLEs.
- **Address OOB in base58 decoder**: Corrects a technical flaw that could allow an out-of-bounds memory read in the Base58 decoder.
- **Add `NegativeUNL` as a supported amendment**: Introduces an amendment for the Negative UNL feature introduced in `rippled` 1.6.0.

## Version 1.7.2

This the 1.7.2 release of rippled, the reference server implementation of the XRP Ledger protocol. This release protects against the security issue [CVE-2021-3499](https://www.openssl.org/news/secadv/20210325.txt) affecting OpenSSL, adds an amendment to fix an issue with small offers not being properly removed from order books in some cases, and includes various other minor fixes.
Version 1.7.2 supersedes version 1.7.1 and adds fixes for more issues that were discovered during the release cycle.

## Action Required

This release introduces a new amendment to the XRP Ledger protocol: `fixRmSmallIncreasedQOffers`. This amendments is now open for voting according to the XRP Ledger's amendment process, which enables protocol changes following two weeks of >80% support from trusted validators.
If you operate an XRP Ledger server, then you should upgrade to version 1.7.2 within two weeks, to ensure service continuity. The exact time that protocol changes take effect depends on the voting decisions of the decentralized network.
If you operate an XRP Ledger validator, please learn more about this amendment so you can make informed decisions about how your validator votes. If you take no action, your validator begins voting in favor of any new amendments as soon as it has been upgraded. 

### Bug Fixes

- **fixRmSmallIncreasedQOffers Amendment:** This amendment fixes an issue where certain small offers can be left at the tip of an order book without being consumed or removed when appropriate and causes some payments and Offers to fail when they should have succeeded [(#3827)](https://github.com/ripple/rippled/pull/3827).
- **Adjust OpenSSL defaults and mitigate CVE-2021-3499:** Prior to this fix, servers compiled against a vulnerable version of OpenSSL could have a crash triggered by a malicious network connection. This fix disables renegotiation support in OpenSSL so that the rippled server is not vulnerable to this bug regardless of the OpenSSL version used to compile the server. This also removes support for deprecated TLS versions 1.0 and 1.1 and ciphers that are not part of TLS 1.2 [(#79e69da)](https://github.com/ripple/rippled/pull/3843/commits/79e69da3647019840dca49622621c3d88bc3883f).
- **Support HTTP health check in reporting mode:** Enables the Health Check special method when running the server in the new Reporting Mode introduced in 1.7.0 [(9c8cadd)](https://github.com/ripple/rippled/pull/3843/commits/9c8caddc5a197bdd642556f8beb14f06d53cdfd3).
- **Maintain compatibility for forwarded RPC responses:** Fixes a case in API responses from servers in Reporting Mode, where requests that were forwarded to a P2P-mode server would have the result field nested inside another result field [(8579eb0)](https://github.com/ripple/rippled/pull/3843/commits/8579eb0c191005022dcb20641444ab471e277f67).
- **Add load_factor in reporting mode:** Adds a load_factor value to the server info method response when running the server in Reporting Mode so that the response is compatible with the format returned by servers in P2P mode (the default) [(430802c)](https://github.com/ripple/rippled/pull/3843/commits/430802c1cf6d4179f2249a30bfab9eff8e1fa748).
- **Properly encode metadata from tx RPC command:** Fixes a problem where transaction metadata in the tx API method response would be in JSON format even when binary was requested [(7311629)](https://github.com/ripple/rippled/pull/3843/commits/73116297aa94c4acbfc74c2593d1aa2323b4cc52).
- **Updates to Windows builds:** When building on Windows, use vcpkg 2021 by default and add compatibility with MSVC 2019 [(36fe196)](https://github.com/ripple/rippled/pull/3843/commits/36fe1966c3cd37f668693b5d9910fab59c3f8b1f), [(30fd458)](https://github.com/ripple/rippled/pull/3843/commits/30fd45890b1d3d5f372a2091d1397b1e8e29d2ca).

## Version 1.7.0 

Ripple has released version 1.7.0 of `rippled`, the reference server implementation of the XRP Ledger protocol. 
This release [significantly improves memory usage](https://blog.ripplex.io/how-ripples-c-team-cut-rippleds-memory-footprint-down-to-size/), introduces a protocol amendment to allow out-of-order transaction execution with Tickets, and brings several other features and improvements. 

## Upgrading (SPECIAL ACTION REQUIRED)
If you use the precompiled binaries of rippled that Ripple publishes for supported platforms, please note that Ripple has renewed the GPG key used to sign these packages. 
If you are upgrading from a previous install, you must download and trust the renewed key. Automatic upgrades will not work until you have re-trusted the key.
### Red Hat Enterprise Linux / CentOS

Perform a [manual upgrade](https://xrpl.org/update-rippled-manually-on-centos-rhel.html). When prompted, confirm that the key's fingerprint matches the following example, then press `y` to accept the updated key:

```
$ sudo yum install rippled
Loaded plugins: fastestmirror
Loading mirror speeds from cached hostfile
* base: mirror.web-ster.com
* epel: mirrors.syringanetworks.net
* extras: ftp.osuosl.org
* updates: mirrors.vcea.wsu.edu
ripple-nightly/signature                                                                                                                                                                                                                                 |  650 B  00:00:00    
Retrieving key from https://repos.ripple.com/repos/rippled-rpm/nightly/repodata/repomd.xml.key
Importing GPG key 0xCCAFD9A2:
Userid     : "TechOps Team at Ripple <techops+rippled@ripple.com>"
Fingerprint: c001 0ec2 05b3 5a33 10dc 90de 395f 97ff ccaf d9a2
From       : https://repos.ripple.com/repos/rippled-rpm/nightly/repodata/repomd.xml.key
Is this ok [y/N]: y
```

### Ubuntu / Debian

Download and trust the updated public key, then perform a [manual upgrade](https://xrpl.org/update-rippled-manually-on-ubuntu.html) as follows:

```
wget -q -O - "https://repos.ripple.com/repos/api/gpg/key/public" | \
    sudo apt-key add -
sudo apt -y update
sudo apt -y install rippled
```

### New and Improved Features

- **Rework deferred node logic and async fetch behavior:** This change significantly improves ledger sync and fetch times while reducing memory consumption.  (https://blog.ripplex.io/how-ripples-c-team-cut-rippleds-memory-footprint-down-to-size/)
- **New Ticket feature:** Tickets are a mechanism to prepare and send certain transactions outside of the normal sequence order. This version reworks and completes the implementation for Tickets after more than 6 years of development. This feature is now open for voting as the newly-introduced `TicketBatch` amendment, which replaces the previously-proposed `Tickets` amendment. The specification for this change can be found at: [xrp-community/standards-drafts#16](https://github.com/xrp-community/standards-drafts/issues/16)
- **Add Reporting Mode:** The server can be compiled to operate in a new mode that serves API requests for validated ledger data without connecting directly to the peer-to-peer network. (The server needs a gRPC connection to another server that is on the peer-to-peer network.) Reporting Mode servers can share access to ledger data via Apache Cassandra and PostgreSQL to more efficiently serve API requests while peer-to-peer servers specialize in broadcasting and processing transactions.
- **Optimize relaying of validation and proposal messages:** Servers typically receive multiple copies of any given message from directly connected peers; in particular, consensus proposal and validation messages are often relayed with extremely high redundancy. For servers with several peers, this can cause redundant work. This commit introduces experimental code that attempts to optimize the relaying of proposals and validations by allowing servers to instruct their peers to "squelch" delivery of selected proposals and validations. This change is considered experimental at this time and is disabled by default because the functioning of the consensus network depends on messages propagating with high reliability through the constantly-changing peer-to-peer network. Server operators who wish to test the optimized code can enable it in their server config file.
- **Report server domain to other servers:** Server operators now have the option to configure a domain name to be associated with their servers. The value is communicated to other servers and is also reported via the `server_info` API. The value is meant for third-party applications and tools to group servers together. For example, a tool that visualizes the network's topology can show how many servers are operated by different stakeholders. An operator can claim any domain, so tools should use the [xrp-ledger.toml file](https://xrpl.org/xrp-ledger-toml.html) to confirm that the domain also claims ownership of the servers.
- **Improve handling of peers that aren't synced:** When evaluating the fitness and usefulness of an outbound peer, the code would incorrectly calculate the amount of time that the peer spent in a non-useful state. This release fixes the calculation and makes the timeout values configurable by server operators. Two new options are introduced in the 'overlay' stanza of the config file. 
- **Persist API-configured voting settings:** Previously, the amendments that a server would vote in support of or against could be configured both via the configuration file and via the ["feature" API method](https://xrpl.org/feature.html). Changes made in the configuration file were only loaded at server startup; changes made via the command line take effect immediately but were not persisted across restarts. Starting with this release, changes made via the API are saved to the wallet.db database file so that they persist even if the server is restarted.
Amendment voting in the config file is deprecated. The first time the server starts with v1.7.0 or higher, it reads any amendment voting settings in the config file and saves the settings to the database; on later restarts the server prints a warning message and ignores the [amendments] and [veto_amendments] stanzas of the config file.
Going forward, use the [feature method](https://xrpl.org/feature.html) to view and configure amendment votes. If you want to use the config file to configure amendment votes, add a line to the [rpc_startup] stanza such as the following:
[rpc_startup]
{ "command": "feature", "feature": "FlowSortStrands", "vetoed": true }
- **Support UNLs with future effective dates:** Updates the format for the recommended validator list file format, allowing publishers to pre-publish the next recommended UNL while the current one is still valid. The server is still backwards compatible with the previous format, but the new format removes some uncertainty during the transition from one list to the next. Also, starting with this release, the server locks down and reports an error if it has no valid validator list. You can clear the error by loading a validator list from a file or by configuring a different UNL and restarting; the error also goes away on its own if the server is able to obtain a trusted validator list from the network (for example, after an network outage resolves itself).
- **Improve manifest relaying:** Servers now propagate change messages for validators' ephemeral public keys ("manifests") on a best-effort basis, to make manifests more available throughout the peer-to-peer network. Previously, the server would only relay manifests from validators it trusts locally, which made it difficult to detect and track validators that are not broadly trusted.
- **Implement ledger forward replay feature:** The server can now sync up to the network by "playing forward" transactions from a previously saved ledger until it catches up to the network. Compared with the default behavior of fetching the latest state and working backwards, forward replay can save time and bandwidth by reconstructing previous ledgers' state data rather than downloading the pre-calculated results from the network. As an added bonus, forward replay confirms that the rest of the network followed the same transaction processing rules as the local server when processing the intervening ledgers. This feature is considered experimental this time and can be enabled with an option in the config file.
- **Make the transaction job queue limit adjustable:** The server uses a job queue to manage tasks, with limits on how many jobs of a particular type can be queued. The previously hard-coded limit associated with transactions is now configurable. Server operators can increase the number of transactions their server is able to queue, which may be useful if your server has a large memory capacity or you expect an influx of transactions. (https://github.com/ripple/rippled/issues/3556)
- **Add public_key to the Validator List method response:** The [Validator List method](https://xrpl.org/validator-list.html) can be used to request a recommended validator list from a rippled instance. The response now includes the public key of the requested list. (https://github.com/ripple/rippled/issues/3392)
- **Server operators can now configure maximum inbound and outbound peers separately:** The new `peers_in_max` and `peers_out_max` config options allow server operators to independently control the maximum number of inbound and outbound peers the server allows. [70c4ecc]
- **Improvements to shard downloading:** Previously the download_shard command could only load shards over HTTPS. Compressed shards can now also be downloaded over plain HTTP. The server fully checks the data for integrity and consistency, so the encryption is not strictly necessary. When initiating multiple shard downloads, the server now returns an error if there is not enough space to store all the shards currently being downloaded.
- **The manifest command is now public:** The manifest API method returns public information about a given validator. The required permissions have been changed so it is now part of the public API.

### Bug Fixes

- **Implement sticky DNS resolution for validator list retrieval:** When attempting to load a validator list from a configured site, attempt to reuse the last IP that was successfully used if that IP is still present in the DNS response. (https://github.com/ripple/rippled/issues/3494).
- **Improve handling of RPC ledger_index argument:** You can now provide the `ledger_index` as a numeric string. This allows you to copy and use the numeric string `ledger_index` value returned by certain RPC commands. Previously you could only send native JSON numbers or shortcut strings such as "validated" in the `ledger_index` field. (https://github.com/ripple/rippled/issues/3533)
- **Fix improper promotion of bool on return**  [6968da1]
- **Fix ledger sequence on copynode** [ef53197] 
-  **Fix parsing of node public keys in `manifest` CLI:** The previous code attempts to validate the provided node public key using a function that assumes that the encoded public key is for an account. This causes the parsing to fail. This commit fixes #3317 (https://github.com/ripple/rippled/issues/3317) by letting the caller specify the type of the public key being checked.
- **Fix idle peer timer:** Fixes a bug where a function to remove idle peers was called every second instead of every 4 seconds. #3754 (https://github.com/ripple/rippled/issues/3754)
- **Add database counters:** Fix bug where DatabaseRotateImp::getBackend and ::sync utilized the writable backend without a lock. ::getBackend was replaced with ::getCounters.
- **Improve online_delete configuration and DB tuning** [6e9051e]
- **Improve handling of burst writes in NuDB database** ( https://github.com/ripple/rippled/pull/3662 )
- **Fix excessive logging after disabling history shards.** Previously if you configured the server with a shard store, then disabled it, the server output excessive warning messages about the shard limit being exceeded.
- **Fixed some issues with negotiating link compression.** ( https://github.com/ripple/rippled/pull/3705 )
- **Fixed a potential thread deadlock with history sharding.** ( https://github.com/ripple/rippled/pull/3683 )
- **Various fixes to typos and comments, refactoring, and build system improvements**

## Version 1.6.0

This release introduces several new features including changes to the XRP Ledger's consensus mechanism to make it even more robust in 
adverse conditions, as well as numerous bug fixes and optimizations.

### New and Improved Features

- Initial implementation of Negative UNL functionality: This change can improve the liveness of the network during periods of network instability, by allowing servers to track which validators are temporarily offline and to adjust quorum calculations to match. This change requires an amendment, but the amendment is not in the **1.6.0** release. Ripple expects to run extensive public testing for Negative UNL functionality on the Devnet in the coming weeks. If public testing satisfies all requirements across security, reliability, stability, and performance, then the amendment could be included in a version 2.0 release. [[#3380](https://github.com/ripple/rippled/pull/3380)]
- Validation Hardening: This change allows servers to detect accidental misconfiguration of validators, as well as potentially Byzantine behavior by malicious validators. Servers can now log a message to notify operators if they detect a single validator issuing validations for multiple, incompatible ledger versions, or validations from multiple servers sharing a key. As part of this update, validators report the version of `rippled` they are using, as well as the hash of the last ledger they consider to be fully validated, in validation messages. [[#3291](https://github.com/ripple/rippled/pull/3291)] ![Amendment: Required](https://img.shields.io/badge/Amendment-Required-red)
- Software Upgrade Monitoring & Notification: After the `HardenedValidations` amendment is enabled and the validators begin reporting the versions of `rippled` they are running, a server can check how many of the validators on its UNL run a newer version of the software than itself. If more than 60% of a server's validators are running a newer version, the server writes a message to notify the operator to consider upgrading their software. [[#3447](https://github.com/ripple/rippled/pull/3447)]
- Link Compression: Beginning with **1.6.0**, server operators can enable support for compressing peer-to-peer messages. This can save bandwidth at a cost of higher CPU usage. This support is disabled by default and should prove useful for servers with a large number of peers. [[#3287](https://github.com/ripple/rippled/pull/3287)]
- Unconditionalize Amendments that were enabled in 2017: This change removes legacy code which the network has not used since 2017. This change limits the ability to [replay](https://github.com/xrp-community/standards-drafts/issues/14) ledgers that rely on the pre-2017 behavior. [[#3292](https://github.com/ripple/rippled/pull/3292)]
- New Health Check Method: Perform a simple HTTP request to get a summary of the health of the server: Healthy, Warning, or Critical. [[#3365](https://github.com/ripple/rippled/pull/3365)]
- Start work on API version 2. Version 2 of the API will be part of a future release. The first breaking change will be to consolidate several closely related error messages that can occur when the server is not synced into a single "notSynced" error message. [[#3269](https://github.com/ripple/rippled/pull/3269)]
- Improved shard concurrency: Improvements to the shard engine have helped reduce the lock scope on all public functions, increasing the concurrency of the code. [[#3251](https://github.com/ripple/rippled/pull/3251)]
- Default Port: In the config file, the `[ips_fixed]` and `[ips]` stanzas now use the [IANA-assigned port](https://www.iana.org/assignments/service-names-port-numbers/service-names-port-numbers.xhtml?search=2459) for the XRP Ledger protocol (2459) when no port is specified. The `connect` API method also uses the same port by default. [[#2861](https://github.com/ripple/rippled/pull/2861)].
- Improve proposal and validation relaying. The peer-to-peer protocol always relays trusted proposals and validations (as part of the [consensus process](https://xrpl.org/consensus.html)), but only relays _untrusted_ proposals and validations in certain circumstances. This update adds configuration options so server operators can fine-tune how their server handles untrusted proposals and validations, and changes the default behavior to prioritize untrusted validations higher than untrusted proposals.  [[#3391](https://github.com/ripple/rippled/pull/3391)]
- Various Build and CI Improvements including updates to RocksDB 6.7.3 [[#3356](https://github.com/ripple/rippled/pull/3356)], NuDB 2.0.3 [[#3437](https://github.com/ripple/rippled/pull/3437)], adjusting CMake settings so that rippled can be built as a submodule [[#3449](https://github.com/ripple/rippled/pull/3449)], and adding Travis CI settings for Ubuntu Bionic Beaver [[#3319](https://github.com/ripple/rippled/pull/3319)].
- Better documentation in the config file for online deletion and database tuning. [[#3429](https://github.com/ripple/rippled/pull/3429)]


### Bug Fixes

- Fix the 14 day timer to enable amendment to start at the correct quorum size [[#3396](https://github.com/ripple/rippled/pull/3396)]
- Improve online delete backend lock which addresses a possibility in the online delete process where one or more backend shared pointer references may become invalid during rotation. [[#3342](https://github.com/ripple/rippled/pull/3342)]
- Address an issue that can occur during the loading of validator tokens, where a deliberately malformed token could cause the server to crash during startup. [[#3326](https://github.com/ripple/rippled/pull/3326)]
- Add delivered amount to GetAccountTransactionHistory. The delivered_amount field was not being populated when calling GetAccountTransactionHistory. In contrast, the delivered_amount field was being populated when calling GetTransaction. This change populates delivered_amount in the response to GetAccountTransactionHistory, and adds a unit test to make sure the results delivered by GetTransaction and GetAccountTransactionHistory match each other. [[#3370](https://github.com/ripple/rippled/pull/3370)]
- Fix build issues for GCC 10 [[#3393](https://github.com/ripple/rippled/pull/3393)]
- Fix historical ledger acquisition - this fixes an issue where historical ledgers were acquired only since the last online deletion interval instead of the configured value to allow deletion.[[#3369](https://github.com/ripple/rippled/pull/3369)]
- Fix build issue with Docker [#3416](https://github.com/ripple/rippled/pull/3416)]
- Add Shard family. The App Family utilizes a single shared Tree Node and Full Below cache for all history shards. This can create a problem when acquiring a shard that shares an account state node that was recently cached from another shard operation. The new Shard Family class solves this issue by managing separate Tree Node and Full Below caches for each shard. [#3448](https://github.com/ripple/rippled/pull/3448)]
- Amendment table clean up which fixes a calculation issue with majority. [#3428](https://github.com/ripple/rippled/pull/3428)]
- Add the `ledger_cleaner` command to rippled command line help [[#3305](https://github.com/ripple/rippled/pull/3305)]
- Various typo and comments fixes.


## Version 1.5.0

The `rippled` 1.5.0 release introduces several improvements and new features, including support for gRPC API, API versioning, UNL propagation via the peer network, new RPC methods `validator_info` and `manifest`, augmented `submit` method, improved `tx` method response, improved command line parsing, improved handshake protocol, improved package building and various other minor bug fixes and improvements.

This release also introduces two new amendments: `fixQualityUpperBound` and `RequireFullyCanonicalSig`.

Several improvements to the sharding system are currently being evaluated for inclusion into the upcoming 1.6 release of `rippled`. These changes are incompatible with shards generated by previous versions of the code. 
Additionally, an issue with the existing sharding engine can result in a server running versions 1.4 or 1.5 of the software to experience a deadlock and automatically restart when running with the sharding feature enabled. 
At this time, the developers recommend running with sharding disabled, pending the improvements scheduled to be introduced with 1.6. For more information on how to disable sharding, please visit https://xrpl.org/configure-history-sharding.html

 
**New and Updated Features**
- The `RequireFullyCanonicalSig` amendment which changes the signature requirements for the XRP Ledger protocol so that non-fully-canonical signatures are no longer valid. This protects against transaction malleability on all transactions, instead of just transactions with the tfFullyCanonicalSig flag enabled. Without this amendment, a transaction is malleable if it uses a secp256k1 signature and does not have tfFullyCanonicalSig enabled. Most signing utilities enable tfFullyCanonicalSig by default, but there are exceptions. With this amendment, no single-signed transactions are malleable. (Multi-signed transactions may still be malleable if signers provide more signatures than are necessary.) All transactions must use the fully canonical form of the signature, regardless of the tfFullyCanonicalSig flag. Signing utilities that do not create fully canonical signatures are not supported. All of Ripple's signing utilities have been providing fully-canonical signatures exclusively since at least 2014. For more information. [`ec137044a`](https://github.com/ripple/rippled/commit/ec137044a014530263cd3309d81643a5a3c1fdab)
- Native [gRPC API](https://grpc.io/) support. Currently, this API provides a subset of the full `rippled` [API](https://xrpl.org/rippled-api.html). You can enable the gRPC API on your server with a new configuration stanza. [`7d867b806`](https://github.com/ripple/rippled/commit/7d867b806d70fc41fb45e3e61b719397033b272c)
- API Versioning which allows for future breaking change of RPC methods to co-exist with existing versions. [`2aa11fa41`](https://github.com/ripple/rippled/commit/2aa11fa41d4a7849ae6a5d7a11df6f367191e3ef)
- Nodes now receive and broadcast UNLs over the peer network under various conditions. [`2c71802e3`](https://github.com/ripple/rippled/commit/2c71802e389a59118024ea0152123144c084b31c)
- Augmented `submit` method to include additional details on the status of the command. [`79e9085dd`](https://github.com/ripple/rippled/commit/79e9085dd1eb72864afe841225b78ec96e72b5ca)
- Improved `tx` method response with additional details on ledgers searched. [`47501b7f9`](https://github.com/ripple/rippled/commit/47501b7f99d4103d9ad405e399169fc251161548)
- New `validator_info` method which returns information pertaining to the current validator's keys, manifest sequence, and domain. [`3578acaf0`](https://github.com/ripple/rippled/commit/3578acaf0b5f2d27ddc33f5b4cc81d21be1903ae)
- New `manifest` method which looks up manifest information for the specified key (either master or ephemeral). [`3578acaf0`](https://github.com/ripple/rippled/commit/3578acaf0b5f2d27ddc33f5b4cc81d21be1903ae)
- Introduce handshake protocol for compression negotiation (compression is not implemented at this point) and other minor improvements. [`f6916bfd4`](https://github.com/ripple/rippled/commit/f6916bfd429ce654e017ae9686cb023d9e05408b)
- Remove various old conditionals introduced by amendments. [`(51ed7db00`](https://github.com/ripple/rippled/commit/51ed7db0027ba822739bd9de6f2613f97c1b227b), [`6e4945c56)`](https://github.com/ripple/rippled/commit/6e4945c56b1a1c063b32921d7750607587ec3063)
- Add `getRippledInfo` info gathering script to `rippled` Linux packages. [`acf4b7889`](https://github.com/ripple/rippled/commit/acf4b78892074303cb1fa22b778da5e7e7eddeda)

**Bug Fixes and Improvements**
- The `fixQualityUpperBound` amendment which fixes a bug in unused code for estimating the ratio of input to output of individual steps in cross-currency payments. [`9d3626fec`](https://github.com/ripple/rippled/commit/9d3626fec5b610100f401dc0d25b9ec8e4a9a362)
- `tx` method now properly fetches all historical tx if they are incorporated into a validated ledger under rules that applied at the time. [`11cf27e00`](https://github.com/ripple/rippled/commit/11cf27e00698dbfc099b23463927d1dac829ed19)
- Fix to how `fail_hard` flag is handled with the `submit` method - transactions that are submitted with the `fail_hard` flag that result in any TER code besides tesSUCCESS is neither queued nor held. [`cd9732b47`](https://github.com/ripple/rippled/commit/cd9732b47a9d4e95bcb74e048d2c76fa118b80fb)
- Remove unused `Beast` code. [`172ead822`](https://github.com/ripple/rippled/commit/172ead822159a3c1f9b73217da4316df48851ab6)
- Lag ratchet code fix to use proper ephemeral public keys instead of the long-term master public keys.[`6529d3e6f`](https://github.com/ripple/rippled/commit/6529d3e6f7333fc5226e5aa9ae65f834cb93dfe5)


## Version 1.4.0

The `rippled` 1.4.0 release introduces several improvements and new features, including support for deleting accounts, improved peer slot management, improved CI integration and package building and support for [C++17](https://en.wikipedia.org/wiki/C%2B%2B17) and [Boost 1.71](https://www.boost.org/users/history/version_1_71_0.html). Finally, this release removes the code for the `SHAMapV2` amendment which failed to gain majority support and has been obsoleted by other improvements.
 
**New and Updated Features**
- The `DeletableAccounts` amendment which, if enabled, will make it possible for users to delete unused or unneeded accounts, recovering the account's reserve.
- Support for improved management of peer slots and the ability to add and removed reserved connections without requiring a restart of the server.
- Tracking and reporting of cumulative and instantaneous peer bandwidth usage. 
- Preliminary support for post-processing historical shards after downloading to index their contents.
- Reporting the master public key alongside the ephemeral public key in the `validation` stream [subscriptions](https://xrpl.org/subscribe.html).
- Reporting consensus phase changes in the `server` stream [subscription](https://xrpl.org/subscribe.html).

**Bug Fixes**
- The `fixPayChanRecipientOwnerDir` amendment which corrects a minor technical flaw that would cause a payment channel to not appear in the recipient's owner directory, which made it unnecessarily difficult for users to enumerate all their payment channels.
- The `fixCheckThreading` amendment which corrects a minor technical flaw that caused checks to not be properly threaded against the account of the check's recipient.
- Respect the `ssl_verify` configuration option in the `SSLHTTPDownloader` and `HTTPClient` classes.
- Properly update the `server_state` when a server detects a disagreement between itself and the network.
- Allow Ed25519 keys to be used with the `channel_authorize` command.

## Version 1.3.1

The `rippled` 1.3.1 release improves the built-in deadlock detection code, improves logging during process startup, changes the package build pipeline and improves the build documentation.

**New and Updated Features**

This release has no new features.

**Bug Fixes**
- Add a LogicError when a deadlock is detected (355a7b04)
- Improve logging during process startup (7c24f7b1)

## Version 1.3.0
The `rippled` 1.3.0 release introduces several new features and overall improvements to the codebase, including the `fixMasterKeyAsRegularKey` amendment, code to adjust the timing of the consensus process and support for decentralized validator domain verification. The release also includes miscellaneous improvements including in the transaction censorship detection code, transaction validation code, manifest parsing code, configuration file parsing code, log file rotation code, and in the build, continuous integration, testing and package building pipelines.

**New and Updated Features**
- The `fixMasterKeyAsRegularKey` amendment which, if enabled, will correct a technical flaw that allowed setting an account's regular key to the account's master key.
- Code that allows validators to adjust the timing of the consensus process in near-real-time to account for connection delays.
- Support for decentralized validator domain verification by adding support for a "domain" field in manifests.

**Bug Fixes**
- Improve ledger trie ancestry tracking to reduce unnecessary error messages.
- More efficient detection of dry paths in the payment engine. Although not a transaction-breaking change, this should reduce spurious error messages in the log files.

## Version 1.2.4

The `rippled` 1.2.4 release improves the way that shard crawl requests are routed and the robustness of configured validator list retrieval by imposing a 20 second timeout.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Use public keys when routing shard crawl requests
- Enforce a 20s timeout when making validator list requests (RIPD-1737)

## Version 1.2.3

The `rippled` 1.2.3 release corrects a technical flaw which in some circumstances can cause a null pointer dereference that can crash the server.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Fix a technical flaw which in some circumstances can cause a null pointer dereference that can crash the server.

## Version 1.2.2

The `rippled` 1.2.2 release corrects a technical flaw in the fee escalation
engine which could cause some fee metrics to be calculated incorrectly. In some
circumstances this can potentially cause the server to crash.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Fix a technical flaw in the fee escalation engine which could cause some fee metrics to be calculated incorrectly (4c06b3f86)

## Version 1.2.1

The `rippled` 1.2.1 release introduces several fixes including a change in the
information reported via the enhanced crawl functionality introduced in the
1.2.0 release, a fix for a potential race condition when processing a status
change message for a peer, and for a technical flaw that could cause a server
to not properly detect that it had lost all its peers.

The release also adds the `delivered_amount` field to more responses to simplify
the handling of payment or check cashing transactions.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Fix a race condition during `TMStatusChange` handling (c8249981)
- Properly transition state to disconnected (9d027394)
- Display validator status only in response to admin requests (2d6a518a)
- Add the `delivered_amount` to more RPC commands (f2756914)


## Version 1.2.0

The `rippled` 1.2.0 release introduces the MultisignReserve Amendment, which
reduces the reserve requirement associated with signer lists. This release also
includes incremental improvements to the code that handles offers. Furthermore,
`rippled` now also has the ability to automatically detect transaction
censorship attempts and issue warnings of increasing severity for transactions
that should have been included in a closed ledger after several rounds of
consensus.

**New and Updated Features**

- Reduce the account reserve for a Multisign SignerList (6572fc8)
- Improve transaction error condition handling (4104778)
- Allow servers to automatically detect transaction censorship attempts (945493d)
- Load validator list from file (c1a0244)
- Add RPC command shard crawl (17e0d09)
- Add RPC Call unit tests (eeb9d92)
- Grow the open ledger expected transactions quickly (7295cf9)
- Avoid dispatching multiple fetch pack threads (4dcb3c9)
- Remove unused function in AutoSocket.h (8dd8433)
- Update TxQ developer docs (e14f913)
- Add user defined literals for megabytes and kilobytes (cd1c5a3)
- Make the FeeEscalation Amendment permanent (58f786c)
- Remove undocumented experimental options from RPC sign (a96cb8f)
- Improve RPC error message for fee command (af1697c)
- Improve ledger_entry command’s inconsistent behavior (63e167b) 

**Bug Fixes**

- Accept redirects from validator list sites (7fe1d4b)
- Implement missing string conversions for JSON (c0e9418) 
- Eliminate potential undefined behavior (c71eb45)
- Add safe_cast to sure no overflow in casts between enums and integral types (a7e4541)

## Version 1.1.2

The `rippled` 1.1.2 release introduces a fix for an issue that could have
prevented cluster peers from successfully bypassing connection limits when
connecting to other servers on the same cluster. Additionally, it improves
logic used to determine what the preferred ledger is during suboptimal
network conditions.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Properly bypass connection limits for cluster peers (#2795, #2796)
- Improve preferred ledger calculation (#2784)

## Version 1.1.1

The `rippled` 1.1.1 release adds support for redirections when retrieving
validator lists and changes the way that validators with an expired list
behave. Additionally, informational commands return more useful information
to allow server operators to determine the state of their server

**New and Updated Features**

- Enhance status reporting when using the `server_info` and `validators` commands (#2734)
- Accept redirects from validator list sites: (#2715)

**Bug Fixes**

- Properly handle expired validator lists when validating (#2734)



## Version 1.1.0

The `rippled` 1.1.0 release release includes the `DepositPreAuth` amendment, which combined with the previously released `DepositAuth` amendment, allows users to pre-authorize incoming transactions to accounts, by whitelisting sender addresses. The 1.1.0 release also includes incremental improvements to several previously released features (`fix1515` amendment), deprecates support for the `sign` and `sign_for` commands from the rippled API and improves invariant checking for enhanced security.

Ripple recommends that all server operators upgrade to XRP Ledger version 1.1.0 by Thursday, 2018-09-27, to ensure service continuity.

**New and Updated Features**

- Add `DepositPreAuth` ledger type and transaction (#2513)  
- Increase fault tolerance and raise validation quorum to 80%, which fixes issue 2604 (#2613)
- Support ipv6 for peer and RPC comms (#2321)
- Refactor ledger replay logic (#2477)
- Improve Invariant Checking (#2532)
- Expand SQLite potential storage capacity (#2650)
- Replace UptimeTimer with UptimeClock (#2532)
- Don’t read Amount field if it is not present (#2566)
- Remove Transactor:: mFeeDue member variable (#2586)
- Remove conditional check for using Boost.Process (#2586)
- Improve charge handling in NoRippleCheckLimits test (#2629)
- Migrate more code into the chrono type system (#2629)
- Supply ConsensusTimer with milliseconds for finer precision (#2629)
- Refactor / modernize Cmake (#2629)
- Add delimiter when appending to cmake_cxx_flags (#2650)
- Remove using namespace declarations at namespace scope in headers (#2650)

**Bug Fixes**

- Deprecate the ‘sign’ and ‘sign_for’ APIs (#2657)
- Use liquidity from strands that consume too many offers, which will be enabled on fix1515 Amendment (#2546)
- Fix a corner case when decoding base64 (#2605)
- Trim space in Endpoint::from_string (#2593)
- Correctly suppress sent messages (#2564)
- Detect when a unit test child process crashes (#2415)
- Handle WebSocket construction exceptions (#2629)
- Improve JSON exception handling (#2605)
- Add missing virtual destructors (#2532)


## Version 1.0.0.

The `rippled` 1.0.0 release includes incremental improvements to several previously released features.

**New and Updated Features**

- The **history sharding** functionality has been improved. Instances can now use the shard store to satisfy ledger requests.
- Change permessage-deflate and compress defaults (RIPD-506)
- Update validations on UNL change (RIPD-1566)

**Bug Fixes**

- Add `check`, `escrow`, and `pay_chan` to `ledger_entry` (RIPD-1600)
- Clarify Escrow semantics (RIPD-1571)


## Version 0.90.1

The `rippled` 0.90.1 release includes fixes for issues reported by external security researchers. These issues, when exploited, could cause a rippled instance to restart or, in some circumstances, stop executing. While these issues can result in a denial of service attack, none affect the integrity of the XRP Ledger and no user funds, including XRP, are at risk.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Address issues identified by external review:
    - Verify serialized public keys more strictly before using them
      (RIPD-1617, RIPD-1619, RIPD-1621)
    - Eliminate a potential out-of-bounds memory access in the base58
      encoding/decoding logic (RIPD-1618)
    - Avoid invoking undefined behavior in memcpy (RIPD-1616)
    - Limit STVar recursion during deserialization (RIPD-1603)
- Use lock when creating a peer shard rangeset


## Version 0.90.0

The `rippled` 0.90.0 release introduces several features and enhancements that improve the reliability, scalability and security of the XRP Ledger.

Highlights of this release include:

- The `DepositAuth` amendment, which lets an account strictly reject any incoming money from transactions sent by other accounts.
- The `Checks` amendment, which allows users to create deferred payments that can be cancelled or cashed by their intended recipients.
- **History Sharding**, which allows `rippled` servers to distribute historical ledger data if they agree to dedicate storage for segments of ledger history.
- New **Preferred Ledger by Branch** semantics which improve the logic that allow a server to decide which ledger it should base future ledgers on when there are multiple candidates.

**New and Updated Features**

- Add support for Deposit Authorization account root flag ([#2239](https://github.com/ripple/rippled/issues/2239))
- Implement history shards ([#2258](https://github.com/ripple/rippled/issues/2258))
- Preferred ledger by branch ([#2300](https://github.com/ripple/rippled/issues/2300))
- Redesign Consensus Simulation Framework ([#2209](https://github.com/ripple/rippled/issues/2209))
- Tune for higher transaction processing ([#2294](https://github.com/ripple/rippled/issues/2294))
- Optimize queries for `account_tx` to work around SQLite query planner ([#2312](https://github.com/ripple/rippled/issues/2312))
- Allow `Journal` to be copied/moved ([#2292](https://github.com/ripple/rippled/issues/2292))
- Cleanly report invalid `[server]` settings ([#2305](https://github.com/ripple/rippled/issues/2305))
- Improve log scrubbing ([#2358](https://github.com/ripple/rippled/issues/2358))
- Update `rippled-example.cfg` ([#2307](https://github.com/ripple/rippled/issues/2307))
- Force json commands to be objects ([#2319](https://github.com/ripple/rippled/issues/2319))
- Fix cmake clang build for sanitizers ([#2325](https://github.com/ripple/rippled/issues/2325))
- Allow `account_objects` RPC to filter by “check” ([#2356](https://github.com/ripple/rippled/issues/2356))
- Limit nesting of json commands ([#2326](https://github.com/ripple/rippled/issues/2326))
- Unit test that `sign_for` returns a correct hash ([#2333](https://github.com/ripple/rippled/issues/2333))
- Update Visual Studio build instructions ([#2355](https://github.com/ripple/rippled/issues/2355))
- Force boost static linking for MacOS builds ([#2334](https://github.com/ripple/rippled/issues/2334))
- Update MacOS build instructions ([#2342](https://github.com/ripple/rippled/issues/2342))
- Add dev docs generation to Jenkins ([#2343](https://github.com/ripple/rippled/issues/2343))
- Poll if process is still alive in Test.py ([#2290](https://github.com/ripple/rippled/issues/2290))
- Remove unused `beast::currentTimeMillis()` ([#2345](https://github.com/ripple/rippled/issues/2345))


**Bug Fixes**
- Improve error message on mistyped command ([#2283](https://github.com/ripple/rippled/issues/2283))
- Add missing includes ([#2368](https://github.com/ripple/rippled/issues/2368))
- Link boost statically only when requested ([#2291](https://github.com/ripple/rippled/issues/2291))
- Unit test logging fixes ([#2293](https://github.com/ripple/rippled/issues/2293))
- Fix Jenkins pipeline for branches ([#2289](https://github.com/ripple/rippled/issues/2289))
- Avoid AppVeyor stack overflow ([#2344](https://github.com/ripple/rippled/issues/2344))
- Reduce noise in log ([#2352](https://github.com/ripple/rippled/issues/2352))


## Version 0.81.0

The `rippled` 0.81.0 release introduces changes that improve the scalability of the XRP Ledger and transitions the recommended validator configuration to a new hosted site, as described in Ripple's [Decentralization Strategy Update](https://ripple.com/dev-blog/decentralization-strategy-update/) post.

**New and Updated Features**

- New hosted validator configuration.


**Bug Fixes**

- Optimize queries for account_tx to work around SQLite query planner ([#2312](https://github.com/ripple/rippled/issues/2312))


## Version 0.80.2

The `rippled` 0.80.2 release introduces changes that improve the scalability of the XRP Ledger.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Do not dispatch a transaction received from a peer for processing if it has already been dispatched within the past ten seconds.
- Increase the number of transaction handlers that can be in flight in the job queue and decrease the relative cost for peers to share transaction and ledger data.
- Make better use of resources by adjusting the number of threads we initialize, by reverting commit [#68b8ffd](https://github.com/ripple/rippled/commit/68b8ffdb638d07937f841f7217edeb25efdb3b5d).

## Version 0.80.1

The `rippled` 0.80.1 release provides several enhancements in support of published validator lists and corrects several bugs.

**New and Updated Features**

- Allow including validator manifests in published list ([#2278](https://github.com/ripple/rippled/issues/2278))
- Add validator list RPC commands ([#2242](https://github.com/ripple/rippled/issues/2242))
- Support [SNI](https://en.wikipedia.org/wiki/Server_Name_Indication) when querying published list sites and use Windows system root certificates ([#2275](https://github.com/ripple/rippled/issues/2275))
- Grow TxQ expected size quickly, shrink slowly ([#2235](https://github.com/ripple/rippled/issues/2235))

**Bug Fixes**

- Make consensus quorum unreachable if validator list expires ([#2240](https://github.com/ripple/rippled/issues/2240))
- Properly use ledger hash to break ties when determing working ledger for consensus ([#2257](https://github.com/ripple/rippled/issues/2257))
- Explictly use std::deque for missing node handler in SHAMap code ([#2252](https://github.com/ripple/rippled/issues/2252))
- Verify validator token manifest matches private key ([#2268](https://github.com/ripple/rippled/issues/2268))


## Version 0.80.0

The `rippled` 0.80.0 release introduces several enhancements that improve the reliability, scalability and security of the XRP Ledger.

Highlights of this release include:

- The `SortedDirectories` amendment, which allows the entries stored within a page to be sorted, and corrects a technical flaw that could, in some edge cases, prevent an empty intermediate page from being deleted.
- Changes to the UNL and quorum rules
  + Use a fixed size UNL if the total listed validators are below threshold
  + Ensure a quorum of 0 cannot be configured
  + Set a quorum to provide Byzantine fault tolerance until a threshold of total validators is exceeded, at which time the quorum is 80%

**New and Updated Features**

- Improve directory insertion and deletion ([#2165](https://github.com/ripple/rippled/issues/2165))
- Move consensus thread safety logic from the generic implementation in Consensus into the RCL adapted version RCLConsensus ([#2106](https://github.com/ripple/rippled/issues/2106))
- Refactor Validations class into a generic version that can be adapted ([#2084](https://github.com/ripple/rippled/issues/2084))
- Make minimum quorum Byzantine fault tolerant ([#2093](https://github.com/ripple/rippled/issues/2093))
- Make amendment blocked state thread-safe and simplify a constructor ([#2207](https://github.com/ripple/rippled/issues/2207))
- Use ledger hash to break ties ([#2169](https://github.com/ripple/rippled/issues/2169))
- Refactor RangeSet ([#2113](https://github.com/ripple/rippled/issues/2113))

**Bug Fixes**

- Fix an issue where `setAmendmentBlocked` is only called when processing the `EnableAmendment` transaction for the amendment ([#2137](https://github.com/ripple/rippled/issues/2137))
- Track escrow in recipient's owner directory ([#2212](https://github.com/ripple/rippled/issues/2212))

## Version 0.70.2

The `rippled` 0.70.2 release corrects an emergent behavior which causes large numbers of transactions to get
stuck in different nodes' open ledgers without being passed on to validators, resulting in a spike in the open
ledger fee on those nodes.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Recent fee rises and TxQ issues ([#2215](https://github.com/ripple/rippled/issues/2215))


## Version 0.70.1

The `rippled` 0.70.1 release corrects a technical flaw in the newly refactored consensus code that could cause a node to get stuck in consensus due to stale votes from a
peer, and allows compiling `rippled` under the 1.1.x releases of OpenSSL.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- Allow compiling against OpenSSL 1.1.0 ([#2151](https://github.com/ripple/rippled/pull/2151))
- Log invariant check messages at "fatal" level ([2154](https://github.com/ripple/rippled/pull/2154))
- Fix the consensus code to update all disputed transactions after a node changes a position ([2156](https://github.com/ripple/rippled/pull/2156))


## Version 0.70.0

The `rippled` 0.70.0 release introduces several enhancements that improve the reliability, scalability and security of the network.

Highlights of this release include:

- The `FlowCross` amendment, which streamlines offer crossing and autobrigding logic by leveraging the new “Flow” payment engine.
- The `EnforceInvariants` amendment, which can safeguard the integrity of the XRP Ledger by introducing code that executes after every transaction and ensures that the execution did not violate key protocol rules.
- `fix1373`, which addresses an issue that would cause payments with certain path specifications to not be properly parsed.

**New and Updated Features**

- Implement and test invariant checks for transactions (#2054)
- TxQ: Functionality to dump all queued transactions (#2020)
- Consensus refactor for simulation/cleanup (#2040)
- Payment flow code should support offer crossing (#1884)
- make `Config` init extensible via lambda (#1993)
- Improve Consensus Documentation (#2064)
- Refactor Dependencies & Unit Test Consensus (#1941)
- `feature` RPC test (#1988)
- Add unit Tests for handlers/TxHistory.cpp (#2062)
- Add unit tests for handlers/AccountCurrenciesHandler.cpp (#2085)
- Add unit test for handlers/Peers.cpp (#2060)
- Improve logging for Transaction affects no accounts warning (#2043)
- Increase logging in PeerImpl fail (#2043)
- Allow filtering of ledger objects by type in RPC (#2066)

**Bug Fixes**

- Fix displayed warning when generating brain wallets (#2121)
- Cmake build does not append '+DEBUG' to the version info for non-unity builds
- Crossing tiny offers can misbehave on RCL
- `asfRequireAuth` flag not always obeyed (#2092)
- Strand creating is incorrectly accepting invalid paths
- JobQueue occasionally crashes on shutdown (#2025)
- Improve pseudo-transaction handling (#2104)

## Version 0.60.3

The `rippled` 0.60.3 release helps to increase the stability of the network under heavy load.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

Server overlay improvements ([#2110](https://github.com/ripple/rippled/pull/2011))

## Version 0.60.2

The `rippled` 0.60.2 release further strengthens handling of cases associated with a previously patched exploit, in which NoRipple flags were being bypassed by using offers.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

Prevent the ability to bypass the `NoRipple` flag using offers ([#7cd4d78](https://github.com/ripple/rippled/commit/4ff40d4954dfaa237c8b708c2126cb39566776da))

## Version 0.60.1

The `rippled` 0.60.1 release corrects a technical flaw that resulted from using 32-bit space identifiers instead of the protocol-defined 16-bit values for Escrow and Payment Channel ledger entries. rippled version 0.60.1 also fixes a problem where the WebSocket timeout timer would not be cancelled when certain errors occurred during subscription streams. Ripple requires upgrading to rippled version 0.60.1 immediately.

**New and Updated Feature**

This release has no new features.

**Bug Fixes**

Correct calculation of Escrow and Payment Channel indices.
Fix WebSocket timeout timer issues.

## Version 0.60.0

The `rippled` 0.60.0 release introduces several enhancements that improve the reliability and scalability of the Ripple Consensus Ledger (RCL), including features that add ledger interoperability by improving Interledger Protocol compatibility. Ripple recommends that all server operators upgrade to version 0.60.0 by Thursday, 2017-03-30, for service continuity.

Highlights of this release include:

- `Escrow` (previously called `SusPay`) which permits users to cryptographically escrow XRP on RCL with an expiration date, and optionally a hashlock crypto-condition. Ripple expects Escrow to be enabled via an Amendment named [`Escrow`](https://ripple.com/build/amendments/#escrow) on Thursday, 2017-03-30. See below for details.
- Dynamic UNL Lite, which allows `rippled` to automatically adjust which validators it trusts based on recommended lists from trusted publishers.

**New and Updated Features**

- Add `Escrow` support (#2039)
- Dynamize trusted validator list and quorum (#1842)
- Simplify fee handling during transaction submission (#1992)
- Publish server stream when fee changes (#2016)
- Replace manifest with validator token (#1975)
- Add validator key revocations (#2019)
- Add `SecretKey` comparison operator (#2004)
- Reduce `LEDGER_MIN_CONSENSUS` (#2013)
- Update libsecp256k1 and Beast B30 (#1983)
- Make `Config` extensible via lambda (#1993)
- WebSocket permessage-deflate integration (#1995)
- Do not close socket on a foreign thread (#2014)
- Update build scripts to support latest boost and ubuntu distros (#1997)
- Handle protoc targets in scons ninja build (#2022)
- Specify syntax version for ripple.proto file (#2007)
- Eliminate protocol header dependency (#1962)
- Use gnu gold or clang lld linkers if available (#2031)
- Add tests for `lookupLedger` (#1989)
- Add unit test for `get_counts` RPC method (#2011)
- Add test for `transaction_entry` request (#2017)
- Unit tests of RPC "sign" (#2010)
- Add failure only unit test reporter (#2018)

**Bug Fixes**

- Enforce rippling constraints during payments (#2049)
- Fix limiting step re-execute bug (#1936)
- Make "wss" work the same as "wss2" (#2033)
- Config test uses unique directories for each test (#1984)
- Check for malformed public key on payment channel (#2027)
- Send a websocket ping before timing out in server (#2035)


## Version 0.50.3

The `rippled` 0.50.3 release corrects a reported exploit that would allow a combination of trust lines and order books in a payment path to bypass the blocking effect of the [`NoRipple`](https://ripple.com/build/understanding-the-noripple-flag/) flag. Ripple recommends that all server operators immediately upgrade to version 0.50.3.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

Correct a reported exploit that would allow a combination of trust lines and order books in a payment path to bypass the blocking effect of the “NoRipple” flag.


## Version 0.50.2

The `rippled` 0.50.2 release adjusts the default TLS cipher list and corrects a flaw that would not allow an SSL handshake to properly complete if the port was configured using the `wss` keyword. Ripple recommends upgrading to 0.50.2 only if server operators are running rippled servers that accept client connections over TLS.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

Adjust the default cipher list and correct a flaw that would not allow an SSL handshake to properly complete if the port was configured using the `wss` keyword (#1985)


## Version 0.50.0

The `rippled` 0.50.0 release includes TickSize, which allows gateways to set a "tick size" for assets they issue to help promote faster price discovery and deeper liquidity, as well as reduce transaction spam and ledger churn on RCL. Ripple expects TickSize to be enabled via an Amendment called TickSize on Tuesday, 2017-02-21.

You can [update to the new version](https://ripple.com/build/rippled-setup/#updating-rippled) on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please [compile the new version from source](https://wiki.ripple.com/Rippled_build_instructions).

**New and Updated Features**

**Tick Size**

Currently, offers on RCL can differ by as little as one part in a quadrillion. This means that there is essentially no value to placing an offer early, as an offer placed later at a microscopically better price gets priority over it. The [TickSize](https://ripple.com/build/amendments/#ticksize) Amendment solves this problem by introducing a minimum tick size that a price must move for an offer to be considered to be at a better price. The tick size is controlled by the issuers of the assets involved.

This change lets issuers quantize the exchange rates of offers to use a specified number of significant digits. Gateways must enable a TickSize on their account for this feature to benefit them. A single AccountSet transaction may set a `TickSize` parameter. Legal values are 0 and 3-15 inclusive. Zero removes the setting. 3-15 allow that many decimal digits of precision in the pricing of offers for assets issued by this account. It will still be possible to place an offer to buy or sell any amount of an asset and the offer will still keep that amount as exactly as it does now. If an offer involves two assets that each have a tick size, the smaller number of significant figures (larger ticks) controls.

For asset pairs with XRP, the tick size imposed, if any, is the tick size of the issuer of the non-XRP asset. For asset pairs without XRP, the tick size imposed, if any, is the smaller of the two issuer's configured tick sizes.

The tick size is imposed by rounding the offer quality down to the nearest tick and recomputing the non-critical side of the offer. For a buy, the amount offered is rounded down. For a sell, the amount charged is rounded up.

The primary expected benefit of the TickSize amendment is the reduction of bots fighting over the tip of the order book, which means:
- Quicker price discovery as outpricing someone by a microscopic amount is made impossible (currently bots can spend hours outbidding each other with no significant price movement)
- A reduction in offer creation and cancellation spam
- Traders can't outbid by a microscopic amount
- More offers left on the books as priority

We also expect larger tick sizes to benefit market makers in the following ways:
- They increase the delta between the fair market value and the trade price, ultimately reducing spreads
- They prevent market makers from consuming each other's offers due to slight changes in perceived fair market value, which promotes liquidity
- They promote faster price discovery by reducing the back and forths required to move the price by traders who don't want to move the price more than they need to
- They reduce transaction spam by reducing fighting over the tip of the order book and reducing the need to change offers due to slight price changes
- They reduce ledger churn and metadata sizes by reducing the number of indexes each order book must have
- They allow the order book as presented to traders to better reflect the actual book since these presentations are inevitably aggregated into ticks

**Hardened TLS configuration**

This release updates the default TLS configuration for rippled. The new release supports only 2048-bit DH parameters and defines a new default set of modern ciphers to use, removing support for ciphers and hash functions that are no longer considered secure.

Server administrators who wish to have different settings can configure custom global and per-port cipher suites in the configuration file using the `ssl_ciphers` directive.

**0.50.0 Change Log**

Remove websocketpp support (#1910)

Increase OpenSSL requirements & harden default TLS cipher suites (#1913)

Move test support sources out of ripple directory (#1916)

Enhance ledger header RPC commands (#1918)

Add support for tick sizes (#1922)

Port discrepancy-test.coffee to c++ (#1930)

Remove redundant call to `clearNeedNetworkLedger` (#1931)

Port freeze-test.coffee to C++ unit test. (#1934)

Fix CMake docs target to work if `BOOST_ROOT` is not set (#1937)

Improve setup for account_tx paging test (#1942)

Eliminate npm tests (#1943)

Port uniport js test to cpp (#1944)

Enable amendments in genesis ledger (#1944)

Trim ledger data in Discrepancy_test (#1948)

Add `current_ledger` field to `fee` result (#1949)

Cleanup unit test support code (#1953)

Add ledger save / load tests (#1955)

Remove unused websocket files (#1957)

Update RPC handler role/usage (#1966)

**Bug Fixes**

Validator's manifest not forwarded beyond directly connected peers (#1919)

**Upcoming Features**

We expect the previously announced Suspended Payments feature, which introduces new transaction types to the Ripple protocol that will permit users to cryptographically escrow XRP on RCL, to be enabled via the [SusPay](https://ripple.com/build/amendments/#suspay) Amendment on Tuesday, 2017-02-21.

Also, we expect support for crypto-conditions, which are signature-like structures that can be used with suspended payments to support ILP integration, to be included in the next rippled release scheduled for March.

Lastly, we do not have an update on the previously announced changes to the hash tree structure that rippled uses to represent a ledger, called [SHAMapV2](https://ripple.com/build/amendments/#shamapv2). At the time of activation, this amendment will require brief scheduled allowable unavailability while the changes to the hash tree structure are computed by the network. We will keep the community updated as we progress towards this date (TBA).


## Version 0.40.1

The `rippled` 0.40.1 release  increases SQLite database limits in all rippled servers. Ripple recommends upgrading to 0.40.1 only if server operators are running rippled servers with full-history of the ledger. There are no new or updated features in the 0.40.1 release.

You can update to the new version on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please compile the new version from source.

**New and Updated Features**

This release has no new features.

**Bug Fixes**

Increase SQLite database limits to prevent full-history servers from crashing when restarting. (#1961)

## Version 0.40.0

The `rippled` 0.40.0 release includes Suspended Payments, a new transaction type on the Ripple network that functions similar to an escrow service, which permits users cryptographically escrow XRP on RCL with an expiration date. Ripple expects Suspended Payments to be enabled via an Amendment named [SusPay](https://ripple.com/build/amendments/#suspay) on Tuesday, 2017-01-17.

You can update to the new version on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please compile the new version from source.

**New and Updated Features**

Previously, Ripple announced the introduction of Payment Channels during the release of rippled version 0.33.0, which permit scalable, off-ledger checkpoints of high volume, low value payments flowing in a single direction. This was the first step in a multi-phase effort to make RCL more scalable and to support Interledger Protocol (ILP). Ripple expects Payment Channels to be enabled via an Amendment called [PayChan](https://ripple.com/build/amendments/#paychan) on a future date to be determined.

In the second phase towards making RCL more scalable and compatible with ILP, Ripple is introducing Suspended Payments, a new transaction type on the Ripple network that functions similar to an escrow service, which permits users to cryptographically escrow XRP on RCL with an expiration date. Ripple expects Suspended Payments to be enabled via an Amendment named [SusPay](https://ripple.com/build/amendments/#suspay) on Tuesday, 2017-01-17.

A Suspended Payment can be created, which deducts the funds from the sending account. It can then be either fulfilled or canceled. It can only be fulfilled if the fulfillment transaction makes it into a ledger with a CloseTime lower than the expiry date of the transaction. It can be canceled with a transaction that makes it into a ledger with a CloseTime greater than the expiry date of the transaction.

In the third phase towards making RCL more scalable and compatible with ILP, Ripple plans to introduce additional library support for crypto-conditions, which are distributable event descriptions written in a standard format that describe how to recognize a fulfillment message without saying exactly what the fulfillment is. Fulfillments are cryptographically verifiable messages that prove an event occurred. If you transmit a fulfillment, then everyone who has the condition can agree that the condition has been met. Fulfillment requires the submission of a signature that matches the condition (message hash and public key). This format supports multiple algorithms, including different hash functions and cryptographic signing schemes. Crypto-conditions can be nested in multiple levels, with each level possibly having multiple signatures.

Lastly, we do not have an update on the previously announced changes to the hash tree structure that rippled uses to represent a ledger, called [SHAMapV2](https://ripple.com/build/amendments/#shamapv2). This will require brief scheduled allowable downtime while the changes to the hash tree structure are propagated by the network. We will keep the community updated as we progress towards this date (TBA).

Consensus refactor (#1874)

Bug Fixes

Correct an issue in payment flow code that did not remove an unfunded offer (#1860)

Sign validator manifests with both ephemeral and master keys (#1865)

Correctly parse multi-buffer JSON messages (#1862)


## Version 0.33.0

The `rippled` 0.33.0 release includes an improved version of the payment code, which we expect to be activated via Amendment on Wednesday, 2016-10-20 with the name [Flow](https://ripple.com/build/amendments/#flow). We are also introducing XRP Payment Channels, a new structure in the ledger designed to support [Interledger Protocol](https://interledger.org/) trust lines as balances get substantial, which we expect to be activated via Amendment on a future date (TBA) with the name [PayChan](https://ripple.com/build/amendments/#paychan). Lastly, we will be introducing changes to the hash tree structure that rippled uses to represent a ledger, which we expect to be available via Amendment on a future date (TBA) with the name [SHAMapV2](https://ripple.com/build/amendments/#shamapv2).

You can [update to the new version](https://ripple.com/build/rippled-setup/#updating-rippled) on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please [compile the new version from source](https://wiki.ripple.com/Rippled_build_instructions).

** New and Updated Features **

A fixed version of the new payment processing engine, which we initially announced on Friday, 2016-07-29, is expected to be available via Amendment on Wednesday, 2016-10-20 with the name [Flow](https://ripple.com/build/amendments/#flow). The new payments code adds no new features, but improves efficiency and robustness in payment handling.

The Flow code may occasionally produce slightly different results than the old payment processing engine due to the effects of floating point rounding.

We will be introducing changes to the hash tree structure that rippled uses to represent a ledger, which we expect to be activated via Amendment on a future date (TBA) with the name [SHAMapV2](https://ripple.com/build/amendments/#shamapv2). The new structure is more compact and efficient than the previous version. This affects how ledger hashes are calculated, but has no other user-facing consequences. The activation of the SHAMapV2 amendment will require brief scheduled allowable downtime, while the changes to the hash tree structure are propagated by the network. We will keep the community updated as we progress towards this date (TBA).

In an effort to make RCL more scalable and to support Interledger Protocol (ILP) trust lines as balances get more substantial, we’re introducing XRP Payment Channels, a new structure in the ledger, which we expect to be available via Amendment on a future date (TBA) with the name [PayChan](https://ripple.com/build/amendments/#paychan).

XRP Payment Channels permit scalable, intermittent, off-ledger settlement of ILP trust lines for high volume payments flowing in a single direction. For bidirectional channels, an XRP Payment Channel can be used in each direction. The recipient can claim any unpaid balance at any time. The owner can top off the channel as needed. The owner must wait out a delay to close the channel to give the recipient a chance to supply any claims. The total amount paid increases monotonically as newer claims are issued.

The initial concept behind payment channels was discussed as early as 2011 and the first implementation was done by Mike Hearn in bitcoinj. Recent work being done by Lightning Network has showcased examples of the many use cases for payment channels. The introduction of XRP Payment Channels allows for a more efficient integration between RCL and ILP to further support enterprise use cases for high volume payments.

Added `getInfoRippled.sh` support script to gather health check for rippled servers [RIPD-1284]

The `account_info` command can now return information about queued transactions - [RIPD-1205]

Automatically-provided sequence numbers now consider the transaction queue - [RIPD-1206]

The `server_info` and `server_state` commands now include the queue-related escalated fee factor in the load_factor field of the response - [RIPD-1207]

A transaction with a high transaction cost can now cause transactions from the same sender queued in front of it to get into the open ledger if the transaction costs are high enough on average across all such transactions. - [RIPD-1246]

Reorganization: Move `LoadFeeTrack` to app/tx and clean up functions - [RIPD-956]

Reorganization: unit test source files -  [RIPD-1132]

Reorganization: NuDB stand-alone repository - [RIPD-1163]

Reorganization: Add `BEAST_EXPECT` to Beast - [RIPD-1243]

Reorganization: Beast 64-bit CMake/Bjam target on Windows - [RIPD-1262]

** Bug Fixes **

`PaymentSandbox::balanceHook` can return the wrong issuer, which could cause the transfer fee to be incorrectly by-passed in rare circumstances. [RIPD-1274, #1827]

Prevent concurrent write operations in websockets [#1806]

Add HTTP status page for new websocket implementation [#1855]


## Version 0.32.1

The `rippled` 0.32.1 release includes an improved version of the payment code, which we expect to be available via Amendment on Wednesday, 2016-08-24 with the name FlowV2, and a completely new implementation of the WebSocket protocol for serving clients.

You can [update to the new version](https://ripple.com/build/rippled-setup/#updating-rippled) on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please [compile the new version from source](https://wiki.ripple.com/Rippled_build_instructions).

**New and Updated Features**

An improved version of the payment processing engine, which we expect to be available via Amendment on Wednesday, 2016-08-24 with the name “FlowV2”. The new payments code adds no new features, but improves efficiency and robustness in payment handling.

The FlowV2 code may occasionally produce slightly different results than the old payment processing engine due to the effects of floating point rounding. Once FlowV2 is enabled on the network then old servers without the FlowV2 amendment will lose sync more frequently because of these differences.

**Beast WebSocket**

A completely new implementation of the WebSocket protocol for serving clients is available as a configurable option for `rippled` administrators. To enable this new implementation, change the “protocol” field in `rippled.cfg` from “ws” to “ws2” (or from “wss” to “wss2” for Secure WebSockets), as illustrated in this example:

    [port_ws_public]
    port = 5006
    ip = 0.0.0.0
    protocol = wss2

The new implementation paves the way for increased reliability and future performance when submitting commands over WebSocket. The behavior and syntax of commands should be identical to the previous implementation. Please report any issues to support@ripple.com. A future version of rippled will remove the old WebSocket implementation, and use only the new one.

**Bug fixes**

Fix a non-exploitable, intermittent crash in some client pathfinding requests (RIPD-1219)

Fix a non-exploitable crash caused by a race condition in the HTTP server. (RIPD-1251)

Fix bug that could cause a previously fee queued transaction to not be relayed after being in the open ledger for an extended time without being included in a validated ledger. Fix bug that would allow an account to have more than the allowed limit of transactions in the fee queue. Fix bug that could crash debug builds in rare cases when replacing a dropped transaction. (RIPD-1200)

Remove incompatible OS X switches in Test.py (RIPD-1250)

Autofilling a transaction fee (sign / submit) with the experimental `x-queue-okay` parameter will use the user’s maximum fee if the open ledger fee is higher, improving queue position, and giving the tx more chance to succeed. (RIPD-1194)



## Version 0.32.0

The `rippled` 0.32.0 release improves transaction queue which now supports batching and can hold up to 10 transactions per account, allowing users to queue multiple transactions for processing when the network load is high. Additionally, the `server_info` and `server_state` commands now include information on transaction cost multipliers and the fee command is available to unprivileged users. We advise rippled operators to upgrade immediately.

You can update to the new version on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please compile the new version from source.

**New and Updated Features**

- Allow multiple transactions per account in transaction queue (RIPD-1048). This also introduces a new transaction engine code, `telCAN_NOT_QUEUE`.
- Charge pathfinding consumers per source currency (RIPD-1019): The IP address used to perform pathfinding operations is now charged an additional resource increment for each source currency in the path set.
- New implementation of payment processing engine. This implementation is not yet enabled by default.
- Include amendments in validations subscription
- Add C++17 compatibility
- New WebSocket server implementation with Beast.WebSocket library. The new library offers a stable, high-performance websocket server implementation. To take advantage of this implementation, change websocket protocol under rippled.cfg from wss and ws to wss2 and ws2 under `[port_wss_admin]` and `[port_ws_public]` stanzas:
```
     [port_wss_admin]
     port = 51237
     ip = 127.0.0.1
     admin = 127.0.0.1
     protocol = wss2

     [port_ws_public]
     port = 51233
     ip = 0.0.0.0
     protocol = wss2, ws2
```
- The fee command is now public (RIPD-1113)
- The fee command checks open ledger rules (RIPD-1183)
- Log when number of available file descriptors is insufficient (RIPD-1125)
- Publish all validation fields for signature verification
- Get quorum and trusted master validator keys from validators.txt
- Standalone mode uses temp DB files by default (RIPD-1129): If a [database_path] is configured, it will always be used, and tables will be upgraded on startup.
- Include config manifest in server_info admin response (RIPD-1172)

**Bug fixes**

- Fix history acquire check (RIPD-1112)
- Correctly handle connections that fail security checks (RIPD-1114)
- Fix secured Websocket closing
- Reject invalid MessageKey in SetAccount handler (RIPD-308, RIPD-990)
- Fix advisory delete effect on history acquisition (RIPD-1112)
- Improve websocket send performance (RIPD-1158)
- Fix XRP bridge payment bug (RIPD-1141)
- Improve error reporting for wallet_propose command. Also include a warning if the key used may be an insecure, low-entropy key. (RIPD-1110)

**Deprecated features**

- Remove obsolete sendGetPeers support (RIPD-164)
- Remove obsolete internal command (RIPD-888)




## Version 0.31.2

The `rippled` 0.31.2 release corrects issues with the fee escalation algorithm. We advise `rippled` operators to upgrade immediately.

You can [update to the new version](https://ripple.com/build/rippled-setup/#updating-rippled) on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please [compile the new version from source](https://wiki.ripple.com/Rippled_build_instructions).

**New and Updated Features**

This release has no new features.

**Bug Fixes**

- A defect in the fee escalation algorithm that caused network fees to escalate more rapidly than intended has been corrected. (RIPD-1177)
- The minimum local fee advertised by validators will no longer be adjusted upwards.



## Version 0.31.1

The `rippled` 0.31.1 release contains one important bug fix. We advise `rippled` operators to upgrade immediately.

You can [update to the new version](https://ripple.com/build/rippled-setup/#updating-rippled) on Red Hat Enterprise Linux 7 or CentOS 7 using yum. For other platforms, please [compile the new version from source](https://wiki.ripple.com/Rippled_build_instructions).

**New and Updated Features**

This release has no new features.

**Bug Fixes**

`rippled` 0.31.1 contains the following fix:

- Correctly handle ledger validations with no `LedgerSequence` field. Previous versions of `rippled` incorrectly assumed that the optional validation field would always be included. Current versions of the software always include the field, and gracefully handle its absence.



## Version 0.31.0

`rippled` 0.31.0 has been released.

You can [update to the new version](https://ripple.com/build/rippled-setup/#updating-rippled) on Red Hat Enterprise Linux 7 or CentOS 7 using yum.

For other platforms, please [compile the new version from source](https://wiki.ripple.com/Rippled_build_instructions). Use the `git log` command to confirm you have the correct source tree. The first log entry should be the change setting the version:


     commit a5d58566386fd86ae4c816c82085fe242b255d2c
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Sun Apr 17 18:02:02 2016 -0700

         Set version to 0.31.0


**Warnings**

Please expect a one-time delay when starting 0.31.0 while certain database indices are being built or rebuilt. The delay can be up to five minutes, during which CPU will spike and the server will appear unresponsive (no response to RPC, etc.).

Additionally, `rippled` 0.31.0 now checks at start-up time that it has sufficient open file descriptors available, and shuts down with an error message if it does not. Previous versions of `rippled` could run out of file descriptors unexpectedly during operation. If you get a file-descriptor error message, increase the number of file descriptors available to `rippled` (for example, editing `/etc/security/limits.conf`) and restart.

**New and Updated Features**

`rippled` 0.31.0 has the following new or updated features:

- (New) [**Amendments**](https://ripple.com/build/amendments/) - A consensus-based system for introducing changes to transaction processing.
- (New) [**Multi-Signing**](https://ripple.com/build/transactions/#multi-signing) - (To be enabled as an amendment) Allow transactions to be authorized by a list of signatures. (RIPD-182)
- (New) **Transaction queue and FeeEscalation** - (To be enabled as an amendment) Include or defer transactions based on the [transaction cost](https://ripple.com/build/transaction-cost/) offered, for better behavior in DDoS conditions. (RIPD-598)
- (Updated) Validations subscription stream now includes `ledger_index` field. (DEC-564)
- (Updated) You can request SignerList information in the `account_info` command (RIPD-1061)

**Closed Issues**

`rippled` 0.31.0 has the following fixes and improvements:

- Improve held transaction submission
- Update SQLite from 3.8.11.1 to 3.11.0
- Allow random seed with specified wallet_propose key_type (RIPD-1030)
- Limit pathfinding source currency limits (RIPD-1062)
- Speed up out of order transaction processing (RIPD-239)
- Pathfinding optimizations
- Streamlined UNL/validator list: The new code removes the ability to specify domain names in the [validators] configuration block, and no longer supports the [validators_site] option.
- Add websocket client
- Add description of rpcSENDMAX_MALFORMED error
- Convert PathRequest to use std::chrono (RIPD-1069)
- Improve compile-time OpenSSL version check
- Clear old Validations during online delete (RIPD-870)
- Return correct error code during unfunded offer cross (RIPD-1082)
- Report delivered_amount for legacy account_tx queries.
- Improve error message when signing fails (RIPD-1066)
- Fix websocket deadlock




## Version 0.30.1

rippled 0.30.1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.30.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit c717006c44126aa0edb3a36ca29ee30e7a72c1d3
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Wed Feb 3 14:49:07 2016 -0800

       Set version to 0.30.1

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.30.1) for more detailed information.

**Release Overview**

The rippled team is proud to release rippled version 0.30.1. This version contains a several minor new features as well as significant improvements to the consensus algorithm that make it work faster and with more consistency. In the time we have been testing the new release on our validators, these changes have led to increased agreement and shorter close times between ledger versions, for approximately 40% more ledgers validated per day.

**New Features**

-   Secure gateway: configured IPs can forward identifying user data in HTTP headers, including user name and origin IP. If the user name exists, then resource limits are lifted for that session. See rippled-example.cfg for more information.
-   Allow fractional fee multipliers (RIPD-626)
-   Add “expiration” to account\_offers (RIPD-1049)
-   Add “owner\_funds” to “transactions” array in RPC ledger (RIPD-1050)
-   Add "tx" option to "ledger" command line
-   Add server uptime in server\_info
-   Allow multiple incoming connections from the same IP
-   Report connection uptime in peer command (RIPD-927)
-   Permit pathfinding to be disabled by setting \[path\_search\_max\] to 0 in rippled.cfg file (RIPD-271)
-   Add subscription to peer status changes (RIPD-579)

**Improvements**

-   Improvements to ledger\_request response
-   Improvements to validations proposal relaying (RIPD-1057)
-   Improvements to consensus algorithm
-   Ledger close time optimizations (RIPD-998, RIPD-791)
-   Delete unfunded offers in predictable order

**Development-Related Updates**

-   Require boost 1.57
-   Implement new coroutines (RIPD-1043)
-   Force STAccount interface to 160-bit size (RIPD-994)
-   Improve compile-time OpenSSL version check

**Bug Fixes**

-   Fix handling of secp256r1 signatures (RIPD-1040)
-   Fix websocket messages dispatching
-   Fix pathfinding early response (RIPD-1064)
-   Handle account\_objects empty response (RIPD-958)
-   Fix delivered\_amount reporting for minor ledgers (RIPD-1051)
-   Fix setting admin privileges on websocket
-   Fix race conditions in account\_tx command (RIPD-1035)
-   Fix to enforce no-ripple constraints

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.30.0

rippled 0.30.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.30.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit a8859b495b552fe1eb140771f0f2cb32d11d2ac2
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Wed Oct 21 18:26:02 2015 -0700

        Set version to 0.30.0

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.30.0) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward protocol security, the rippled team would like to release rippled 0.30.0.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**`grep '^processor' /proc/cpuinfo | wc -l`**), you can use them to assist in the build process by compiling with the command **`scons -j[number of CPUs - 1]`**.

**New Features**

-   Honor markers in ledger\_data requests ([RIPD-1010](https://ripplelabs.atlassian.net/browse/RIPD-1010)).
-   New Amendment - **TrustSetAuth** (Not currently enabled) Create zero balance trust lines with auth flag ([RIPD-1003](https://ripplelabs.atlassian.net/browse/RIPD-1003)): this allows a TrustSet transaction to create a trust line if the only thing being changed is setting the tfSetfAuth flag.
-   Randomize the initial transaction execution order for closed ledgers based on the hash of the consensus set ([RIPD-961](https://ripplelabs.atlassian.net/browse/RIPD-961)). **Activates on October 27, 2015 at 11:00 AM PCT**.
-   Differentiate path\_find response ([RIPD-1013](https://ripplelabs.atlassian.net/browse/RIPD-1013)).
-   Convert all of an asset ([RIPD-655](https://ripplelabs.atlassian.net/browse/RIPD-655)).

**Improvements**

-   SHAMap improvements.
-   Upgrade SQLite from 3.8.8.2 to 3.8.11.1.
-   Limit the number of offers that can be consumed during crossing ([RIPD-1026](https://ripplelabs.atlassian.net/browse/RIPD-1026)).
-   Remove unfunded offers on tecOVERSIZE ([RIPD-1026](https://ripplelabs.atlassian.net/browse/RIPD-1026)).
-   Improve transport security ([RIPD-1029](https://ripplelabs.atlassian.net/browse/RIPD-1029)): to take full advantage of the improved transport security, servers with a single, static public IP address should add it to their configuration file, as follows:

     [overlay]
     public_ip=<ip_address>

**Development-Related Updates**

-   Transitional support for gcc 5.2: to enable support define the environmental variable `RIPPLED_OLD_GCC_ABI`=1
-   Transitional support for C++ 14: to enable support define the environment variable `RIPPLED_USE_CPP_14`=1
-   Visual Studio 2015 support
-   Updates to integration tests
-   Add uptime to crawl data ([RIPD-997](https://ripplelabs.atlassian.net/browse/RIPD-997))

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.29.0

rippled 0.29.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/commits/0.29.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 5964710f736e258c7892e8b848c48952a4c7856c
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Tue Aug 4 13:22:45 2015 -0700

        Set version to 0.29.0

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.29.0) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward protocol security, the rippled team would like to announce rippled release 0.29.0.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

**New Features**

-   Subscription stream for validations ([RIPD-504](https://ripplelabs.atlassian.net/browse/RIPD-504))

**Deprecated features**

-   Disable Websocket ping timer

**Bug Fixes**

-   Fix off-by one bug that overstates the account reserve during OfferCreate transaction. **Activates August 17, 2015**.
-   Fix funded offer removal during Payment transaction ([RIPD-113](https://ripplelabs.atlassian.net/browse/RIPD-113)). **Activates August 17, 2015**.
-   Fix display discrepancy in fee.

**Improvements**

-   Add “quality” field to account\_offers API response: quality is defined as the exchange rate, the ratio taker\_pays divided by taker\_gets.
-   Add [full\_reply](https://ripple.com/build/rippled-apis/#path-find-create) field to path\_find API response: full\_reply is defined as true/false value depending on the completed depth of pathfinding search ([RIPD-894](https://ripplelabs.atlassian.net/browse/RIPD-894)).
-   Add [DeliverMin](https://ripple.com/build/transactions/#payment) transaction field ([RIPD-930](https://ripplelabs.atlassian.net/browse/RIPD-930)). **Activates August 17, 2015**.

**Development-Related Updates**

-   Add uptime to crawl data ([RIPD-997](https://ripplelabs.atlassian.net/browse/RIPD-997)).
-   Add IOUAmount and XRPAmount: these numeric types replace the monolithic functionality found in STAmount ([RIPD-976](https://ripplelabs.atlassian.net/browse/RIPD-976)).
-   Log metadata differences on built ledger mismatch.
-   Add enableTesting flag to applyTransactions.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.28.2

rippled 0.28.2 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/commits/release>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 6374aad9bc94595e051a04e23580617bc1aaf300
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Tue Jul 7 09:21:44 2015 -0700

        Set version to 0.28.2

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/release) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward protocol security, the rippled team would like to announce rippled release 0.28.2. **This release is necessary for compatibility with OpenSSL v.1.0.1n and later.**

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**rippled.cfg Updates**

For \[ips\] stanza, a port must be specified for each listed IP address with the space between IP address and port, ex.: `r.ripple.com` `51235` ([RIPD-893](https://ripplelabs.atlassian.net/browse/RIPD-893))

**New Features**

-   New API: [gateway\_balances](https://ripple.com/build/rippled-apis/#gateway-balances) to get a gateway's hot wallet balances and total obligations.

**Deprecated features**

-   Removed temp\_db ([RIPD-887](https://ripplelabs.atlassian.net/browse/RIPD-887))

**Improvements**

-   Improve peer send queue management
-   Support larger EDH keys
-   More robust call to get the valid ledger index
-   Performance improvements to transactions application to open ledger

**Development-Related Updates**

-   New Env transaction testing framework for unit testing
-   Fix MSVC link
-   C++ 14 readiness

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.28.1

rippled 0.28.1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.28.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 399c43cae6e90a428e9ce6a988123972b0f03c99
     Author: Miguel Portilla <miguelportilla@pobros.com>
     Date:   Wed May 20 13:30:54 2015 -0400

        Set version to 0.28.1

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.28.1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Filtering for Account Objects ([RIPD-868](https://ripplelabs.atlassian.net/browse/RIPD-868)).
-   Track rippled server peers latency ([RIPD-879](https://ripplelabs.atlassian.net/browse/RIPD-879)).

**Bug fixes**

-   Expedite zero flow handling for offers
-   Fix offer crossing when funds are the limiting factor

**Deprecated features**

-   Wallet\_accounts and generator maps ([RIPD-804](https://ripplelabs.atlassian.net/browse/RIPD-804))

**Improvements**

-   Control ledger query depth based on peers latency
-   Improvements to ledger history fetches
-   Improve RPC ledger synchronization requirements ([RIPD-27](https://ripplelabs.atlassian.net/browse/RIPD-27), [RIPD-840](https://ripplelabs.atlassian.net/browse/RIPD-840))
-   Eliminate need for ledger in delivered\_amount calculation ([RIPD-860](https://ripplelabs.atlassian.net/browse/RIPD-860))
-   Improvements to JSON parsing

**Development-Related Updates**

-   Add historical ledger fetches per minute to get\_counts
-   Compute validated ledger age from signing time
-   Remove unused database table ([RIPD-755](https://ripplelabs.atlassian.net/browse/RIPD-755))

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.28.0

rippled 0.28.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.28.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 7efd0ab0d6ef017331a0e214a3053893c88f38a9
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Fri Apr 24 18:57:36 2015 -0700

        Set version to 0.28.0

This release incorporates a number of important features, bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.28.0) for more detailed information.

**Release Overview**

As part of Ripple Labs’ ongoing commitment toward improving the protocol, the rippled team is excited to announce **autobridging** — a feature that allows XRP to serve as a bridge currency. Autobridging enhances utility and has the potential to expose more of the network to liquidity and improve prices. For more information please refer to the [autobridging blog post](https://ripple.com/uncategorized/introducing-offer-autobridging/).

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Important rippled.cfg update**

With rippled version 0.28, the rippled.cfg file must be changed according to these instructions:

-   Change any entries that say

`admin` `=` `allow` to `admin` `=` <IP address>

-   For most installations, 127.0.0.1 will preserve current behavior. 0.0.0.0 may be specified to indicate "any IP" but cannot be combined with other IP addresses. Use of 0.0.0.0 may introduce severe security risks and is not recommended. See docs/rippled-example.cfg for more information.

**More Strict Requirements on MemoType**

The requirements on the contents of the MemoType field, if present, are more strict than the previous version. Transactions that can successfully be submitted to 0.27.4 and earlier may fail in 0.28.0. For details, please refer to [updated memo documentation](https://ripple.com/build/transactions/#memos) for details. Partners should check their implementation to make sure that their MemoType follows the new rules.

**New Features**

-   Autobridging implementation ([RIPD-423](https://ripplelabs.atlassian.net/browse/RIPD-423)). **This feature will be turned on May 12, 2015**.
-   Combine history\_ledger\_index and online\_delete settings in rippled.cfg ([RIPD-774](https://ripplelabs.atlassian.net/browse/RIPD-774)).
-   Claim a fee when a required destination tag is not specified ([RIPD-574](https://ripplelabs.atlassian.net/browse/RIPD-574)).
-   Require the master key when disabling the use of the master key or when enabling 'no freeze' ([RIPD-666](https://ripplelabs.atlassian.net/browse/RIPD-666)).
-   Change the port setting admin to accept allowable admin IP addresses ([RIPD-820](https://ripplelabs.atlassian.net/browse/RIPD-820)):
    -   rpc\_admin\_allow has been removed.
    -   Comma-separated list of IP addresses that are allowed administrative privileges (subject to username & password authentication if configured).
    -   127.0.0.1 is no longer a default admin IP.
    -   0.0.0.0 may be specified to indicate "any IP" but cannot be combined with other MIP addresses. Use of 0.0.0.0 may introduce severe security risks and is not recommended.
-   Enable Amendments from config file or static data ([RIPD-746](https://ripplelabs.atlassian.net/browse/RIPD-746)).

**Bug fixes**

-   Fix payment engine handling of offer ⇔ account ⇔ offer cases ([RIPD-639](https://ripplelabs.atlassian.net/browse/RIPD-639)). **This fix will take effect on May 12, 2015**.
-   Fix specified destination issuer in pathfinding ([RIPD-812](https://ripplelabs.atlassian.net/browse/RIPD-812)).
-   Only report delivered\_amount for executed payments ([RIPD-827](https://ripplelabs.atlassian.net/browse/RIPD-827)).
-   Return a validated ledger if there is one ([RIPD-814](https://ripplelabs.atlassian.net/browse/RIPD-814)).
-   Refund owner's ticket reserve when a ticket is canceled ([RIPD-855](https://ripplelabs.atlassian.net/browse/RIPD-855)).
-   Return descriptive error from account\_currencies RPC ([RIPD-806](https://ripplelabs.atlassian.net/browse/RIPD-806)).
-   Fix transaction enumeration in account\_tx API ([RIPD-734](https://ripplelabs.atlassian.net/browse/RIPD-734)).
-   Fix inconsistent ledger\_current return ([RIPD-669](https://ripplelabs.atlassian.net/browse/RIPD-669)).
-   Fix flags --rpc\_ip and --rpc\_port ([RIPD-679](https://ripplelabs.atlassian.net/browse/RIPD-679)).
-   Skip inefficient SQL query ([RIPD-870](https://ripplelabs.atlassian.net/browse/RIPD-870))

**Deprecated features**

-   Remove support for deprecated PreviousTxnID field ([RIPD-710](https://ripplelabs.atlassian.net/browse/RIPD-710)). **This will take effect on May 12, 2015**.
-   Eliminate temREDUNDANT\_SEND\_MAX ([RIPD-690](https://ripplelabs.atlassian.net/browse/RIPD-690)).
-   Remove WalletAdd ([RIPD-725](https://ripplelabs.atlassian.net/browse/RIPD-725)).
-   Remove SMS support.

**Improvements**

-   Improvements to peer communications.
-   Reduce master lock for client requests.
-   Update SQLite to 3.8.8.2.
-   Require Boost 1.57.
-   Improvements to Universal Port ([RIPD-687](https://ripplelabs.atlassian.net/browse/RIPD-687)).
-   Constrain valid inputs for memo fields ([RIPD-712](https://ripplelabs.atlassian.net/browse/RIPD-712)).
-   Binary option for ledger command ([RIPD-692](https://ripplelabs.atlassian.net/browse/RIPD-692)).
-   Optimize transaction checks ([RIPD-751](https://ripplelabs.atlassian.net/browse/RIPD-751)).

**Development-Related Updates**

-   Add RPC metrics ([RIPD-705](https://ripplelabs.atlassian.net/browse/RIPD-705)).
-   Track and report peer load.
-   Builds/Test.py will build and test by one or more scons targets.
-   Support a --noserver command line option in tests:
-   Run npm/integration tests without launching rippled, using a running instance of rippled (possibly in a debugger) instead.
-   Works for npm test and mocha.
-   Display human readable SSL error codes.
-   Better transaction analysis ([RIPD-755](https://ripplelabs.atlassian.net/browse/RIPD-755)).

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.27.4

rippled 0.27.4 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.4>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 92812fe7239ffa3ba91649b2ece1e892b866ec2a
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Wed Mar 11 11:26:44 2015 -0700

        Set version to 0.27.4

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.4) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Bug Fixes**

-   Limit passes in the payment engine

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.27.3-sp2

rippled 0.27.3-sp2 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.3-sp2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit f999839e599e131ed624330ad0ce85bb995f02d3
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Thu Mar 12 13:37:47 2015 -0700

        Set version to 0.27.3-sp2

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.3-sp2) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Add noripple\_check RPC command: this command tells gateways what they need to do to set "Default Ripple" account flag and fix any trust lines created before the flag was set.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)




-----------------------------------------------------------

## Version 0.27.3-sp1

rippled 0.27.3-sp1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.3-sp1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 232693419a2c9a8276a0fae991f688f6f01a3add
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Wed Mar 11 10:26:39 2015 -0700

       Set version to 0.27.3-sp1

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.3-sp1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Add "Default Ripple" account flag

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.27.3

rippled 0.27.3 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.3>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 70c2854f7c8a28801a7ebc81dd62bf0d068188f0
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Tue Mar 10 14:06:33 2015 -0700

        Set version to 0.27.3

This release includes one new feature. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.3) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Add "Default Ripple" account flag

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.27.2

rippled 0.27.2 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 9cc8eec773e8afc9c12a6aab4982deda80495cf1
     Author: Nik Bougalis <nikb@bougalis.net>
     Date:   Sun Mar 1 14:56:44 2015 -0800

       Set version to 0.27.2

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.2) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   NuDB backend option: high performance key/value database optimized for rippled (set “type=nudb” in .cfg).
    -   Either import RockdDB to NuDB using import tool, or
    -   Start fresh with NuDB but delete SQLite databases if rippled ran previously with RocksDB:

     rm [database_path]/transaction.* [database_path]/ledger.*

**Bug Fixes**

-   Fix offer quality bug

**Deprecated**

-   HyperLevelDB, LevelDB, and SQLlite backend options. Use RocksDB for spinning drives and NuDB for SSDs backend options.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.27.1

rippled 0.27.1 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 95973ba3e8b0bd28eeaa034da8b806faaf498d8a
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Tue Feb 24 13:31:13 2015 -0800

       Set version to 0.27.1

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   RocksDB to NuDB import tool ([RIPD-781](https://ripplelabs.atlassian.net/browse/RIPD-781), [RIPD-785](https://ripplelabs.atlassian.net/browse/RIPD-785)): custom tool specifically designed for very fast import of RocksDB nodestore databases into NuDB

**Bug Fixes**

-   Fix streambuf bug

**Improvements**

-   Update RocksDB backend settings
-   NuDB improvements:
    -   Limit size of mempool ([RIPD-787](https://ripplelabs.atlassian.net/browse/RIPD-787))
    -   Performance improvements ([RIPD-793](https://ripplelabs.atlassian.net/browse/RIPD-793), [RIPD-796](https://ripplelabs.atlassian.net/browse/RIPD-796)): changes in Nudb to improve speed, reduce database size, and enhance correctness. The most significant change is to store hashes rather than entire keys in the key file. The output of the hash function is reduced to 48 bits, and stored directly in buckets.

**Experimental**

-   Add /crawl cgi request feature to peer protocol ([RIPD-729](https://ripplelabs.atlassian.net/browse/RIPD-729)): adds support for a cgi /crawl request, issued over HTTPS to the configured peer protocol port. The response to the request is a JSON object containing the node public key, type, and IP address of each directly connected neighbor. The IP address is suppressed unless the neighbor has requested its address to be revealed by adding "Crawl: public" to its HTTP headers. This field is currently set by the peer\_private option in the rippled.cfg file.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.27.0

rippled 0.27.0 has been released. The commit can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.27.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit c6c8e5d70c6fbde02cd946135a061aa77744396f
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Jan 26 10:56:11 2015 -0800

         Set version to 0.27.0

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.27.0) for more detailed information.

**Release Overview**

The rippled team is proud to release rippled 0.27.0. This new version includes many exciting features that will appeal to our users. The team continues to work on stability, scalability, and performance.

The first feature is Online Delete. This feature allows rippled to maintain it’s database of previous ledgers within a fixed amount of disk space. It does this while allowing rippled to stay online and maintain an administrator specify minimum number of ledgers. This means administrators with limited disk space will no longer need to manage disk space by periodically manually removing the database. Also, with the previously existing backend databases performance would gradually degrade as the database grew in size. In particular, rippled would perform poorly whenever the backend database performed ever growing compaction operations. By limiting rippled to less history, compaction is less resource intensive and systems with less disk performance can now run rippled.

Additionally, we are very excited to include Universal Port. This feature allows rippled's listening port to handshake in multiple protocols. For example, a single listening port can be configured to receive incoming peer connections, incoming RPC commands over HTTP, and incoming RPC commands over HTTPS at the same time. Or, a single port can receive both Websockets and Secure Websockets clients at the same.

Finally, a new, experimental backend database, NuDB, has been added. This database was developed by Ripple Labs to take advantage of rippled’s specific data usage profile and performs much better than previous databases. Significantly, this database does not degrade in performance as the database grows. Very excitingly, this database works on OS X and Windows. This allows rippled to use these platforms for the first time.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.57.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Important rippled.cfg Update**

**The format of the configuration file has changed. If upgrading from a previous version of rippled, please see the migration instructions below.**

**New Features**

-   SHAMapStore Online Delete ([RIPD-415](https://ripplelabs.atlassian.net/browse/RIPD-415)): Makes rippled configurable to support deletion of all data in its key-value store (nodestore) and ledger and transaction SQLite databases based on validated ledger sequence numbers. See doc/rippled-example.cfg for configuration setup.
-   [Universal Port](https://forum.ripple.com/viewtopic.php?f=2&t=8313&p=57969). See necessary config changes below.
-   Config "ledger\_history\_index" option ([RIPD-559](https://ripplelabs.atlassian.net/browse/RIPD-559))

**Bug Fixes**

-   Fix pathfinding with multiple issuers for one currency ([RIPD-618](https://ripplelabs.atlassian.net/browse/RIPD-618))
-   Fix account\_lines, account\_offers and book\_offers result ([RIPD-682](https://ripplelabs.atlassian.net/browse/RIPD-682))
-   Fix pathfinding bugs ([RIPD-735](https://ripplelabs.atlassian.net/browse/RIPD-735))
-   Fix RPC subscribe with multiple books ([RIPD-77](https://ripplelabs.atlassian.net/browse/RIPD-77))
-   Fix account\_tx API

**Improvements**

-   Improve the human-readable description of the tesSUCCESS code
-   Add 'delivered\_amount' to Transaction JSON ([RIPD-643](https://ripplelabs.atlassian.net/browse/RIPD-643)): The synthetic field 'delivered\_amount' can be used to determine the exact amount delivered by a Payment without having to check the DeliveredAmount field, if present, or the Amount field otherwise.

**Development-Related Updates**

-   HTTP Handshaking for Peers on Universal Port ([RIPD-446](https://ripplelabs.atlassian.net/browse/RIPD-446))
-   Use asio signal handling in Application ([RIPD-140](https://ripplelabs.atlassian.net/browse/RIPD-140))
-   Build dependency on Boost 1.57.0
-   Support a "no\_server" flag in test config
-   API for improved Unit Testing ([RIPD-432](https://ripplelabs.atlassian.net/browse/RIPD-432))
-   Option to specify rippled path on command line (--rippled=\<absolute or relative path\>)

**Experimental**

-   NuDB backend option: high performance key/value database optimized for rippled (set “type=nudb” in .cfg)

**Migration Instructions**

With rippled version 0.27.0, the rippled.cfg file must be changed according to these instructions:

-   Add new stanza - `[server]`. This section will contain a list of port names and key/value pairs. A port name must start with a letter and contain only letters and numbers. The name is not case-sensitive. For each name in this list, rippled will look for a configuration file section with the same name and use it to create a listening port. To simplify migration, you can use port names from your previous version of rippled.cfg (see Section 1. Server for detailed explanation in doc/rippled-example.cfg). For example:

         [server]
         rpc_port
         peer_port
         websocket_port
         ssl_key = <set value to your current [rpc_ssl_key] or [websocket_ssl_key] setting>
         ssl_cert = <set value to your current [rpc_ssl_cert] or [websocket_ssl_cert] setting>
         ssl_chain = <set value to your current [rpc_ssl_chain] or [websocket_ssl_chain] setting>

-   For each port name in `[server]` stanza, add separate stanzas. For example:

         [rpc_port]
         port = <set value to your current [rpc_port] setting, usually 5005>
         ip = <set value to your current [rpc_ip] setting, usually 127.0.0.1>
         admin = allow
         protocol = https

         [peer_port]
         port = <set value to your current [peer_port], usually 51235>
         ip = <set value to your current [peer_ip], usually 0.0.0.0>
         protocol = peer

         [websocket_port]
         port = <your current [websocket_port], usually 6006>
         ip = <your current [websocket_ip], usually 127.0.0.1>
         admin = allow
         protocol = wss

-   Remove current `[rpc_port],` `[rpc_ip],` `[rpc_allow_remote],` `[rpc_ssl_key],` `[rpc_ssl_cert],` `and` `[rpc_ssl_chain],` `[peer_port],` `[peer_ip],` `[websocket_port],` `[websocket_ip]` settings from rippled.cfg

-   If you allow untrusted websocket connections to your rippled, add `[websocket_public_port]` stanza under `[server]` section and replace websocket public settings with `[websocket_public_port]` section:

         [websocket_public_port]
         port = <your current [websocket_public_port], usually 5005>
         ip = <your current [websocket_public_ip], usually 127.0.0.1>
         protocol = ws ← make sure this is ws, not wss`

-   Remove `[websocket_public_port],` `[websocket_public_ip],` `[websocket_ssl_key],` `[websocket_ssl_cert],` `[websocket_ssl_chain]` settings from rippled.cfg
-   Disable `[ssl_verify]` section by setting it to 0
-   Migrate the remaining configurations without changes. To enable online delete feature, check Section 6. Database in doc/rippled-example.cfg

**Integration Notes**

With this release, integrators should deprecate the "DeliveredAmount" field in favor of "delivered\_amount."

**For Transactions That Occurred Before January 20, 2014:**

-   If amount actually delivered is different than the transactions “Amount” field
    -   "delivered\_amount" will show as unavailable indicating a developer should use caution when processing this payment.
    -   Example: A partial payment transaction (tfPartialPayment).
-   Otherwise
    -   "delivered\_amount" will show the correct destination balance change.

**For Transactions That Occur After January 20, 2014:**

-   If amount actually delivered is different than the transactions “Amount” field
    -   A "delivered\_amount" field will determine the destination amount change
    -   Example: A partial payment transaction (tfPartialPayment).
-   Otherwise
    -   "delivered\_amount" will show the correct destination balance change.

**Assistance**

For assistance, please contact **integration@ripple.com**

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.26.4

rippled 0.26.4 has been released. The repository tag is *0.26.4* and can be found on GitHub at: <https://github.com/ripple/rippled/commits/0.26.4>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 05a04aa80192452475888479c84ff4b9b54e6ae7
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Nov 3 16:53:37 2014 -0800

         Set version to 0.26.4

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.4) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Important JSON-RPC Update**

With rippled version 0.26.4, the [rippled.cfg](https://github.com/ripple/rippled/blob/0.26.4/doc/rippled-example.cfg) file must set the ssl\_verify property to 0. Without this update, JSON-RPC API calls may not work.

**New Features**

-   Rocksdb v. 3.5.1
-   SQLite v. 3.8.7
-   Disable SSLv3
-   Add counters to track ledger read and write activities
-   Use trusted validators median fee when determining transaction fee
-   Add --quorum argument for server start ([RIPD-563](https://ripplelabs.atlassian.net/browse/RIPD-563))
-   Add account\_offers paging ([RIPD-344](https://ripplelabs.atlassian.net/browse/RIPD-344))
-   Add account\_lines paging ([RIPD-343](https://ripplelabs.atlassian.net/browse/RIPD-343))
-   Ability to configure network fee in rippled.cfg file ([RIPD-564](https://ripplelabs.atlassian.net/browse/RIPD-564))

**Bug Fixes**

-   Fix OS X version parsing/error related to OS X 10.10 update
-   Fix incorrect address in connectivity check report
-   Fix page sizes for ledger\_data ([RIPD-249](https://ripplelabs.atlassian.net/browse/RIPD-249))
-   Make log partitions case-insensitive in rippled.cfg

**Improvements**

-   Performance
    -   Ledger performance improvements for storage and traversal ([RIPD-434](https://ripplelabs.atlassian.net/browse/RIPD-434))
    -   Improve client performance for JSON responses ([RIPD-439](https://ripplelabs.atlassian.net/browse/RIPD-439))
-   Other
    -   Remove PROXY handshake feature
    -   Change to rippled.cfg to support sections containing both key/value pairs and a list of values
    -   Return descriptive error message for memo validation ([RIPD-591](https://ripplelabs.atlassian.net/browse/RIPD-591))
    -   Changes to enforce JSON-RPC 2.0 error format
    -   Optimize account\_lines and account\_offers ([RIPD-587](https://ripplelabs.atlassian.net/browse/RIPD-587))
    -   Improve fee setting logic ([RIPD-614](https://ripplelabs.atlassian.net/browse/RIPD-614))
    -   Improve transaction security
    -   Config improvements
    -   Improve path filtering ([RIPD-561](https://ripplelabs.atlassian.net/browse/RIPD-561))
    -   Logging to distinguish Byzantine failure from tx bug ([RIPD-523](https://ripplelabs.atlassian.net/browse/RIPD-523))

**Experimental**

-   Add "deferred" flag to transaction relay message (required for future code that will relay deferred transactions)
-   Refactor STParsedJSON to parse an object or array (required for multisign implementation) ([RIPD-480](https://ripplelabs.atlassian.net/browse/RIPD-480))

**Development-Related Updates**

-   Changes to DatabaseReader to read ledger numbers from database
-   Improvements to SConstruct

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.26.3-sp1

rippled 0.26.3-sp1 has been released. The repository tag is *0.26.3-sp1* and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.3-sp1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 2ad6f0a65e248b4f614d38d199a9d5d02f5aaed8
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Fri Sep 12 15:22:54 2014 -0700

         Set version to 0.26.3-sp1

This release incorporates a number of important bugfixes and functional improvements. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.3-sp1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   New command to display HTTP/S-RPC sessions metrics ([RIPD-533](https://ripplelabs.atlassian.net/browse/RIPD-533))

**Bug Fixes**

-   Improved handling of HTTP/S-RPC sessions ([RIPD-489](https://ripplelabs.atlassian.net/browse/RIPD-489))
-   Fix unit tests for Windows.
-   Fix integer overflows in JSON parser.

**Improvements**

-   Improve processing of trust lines during pathfinding.

**Experimental Features**

-   Added a command line utility called LedgerTool for retrieving and processing ledger blocks from the Ripple network.

**Development-Related Updates**

-   HTTP message and parser improvements.
    -   Streambuf wrapper supports rvalue move.
    -   Message class holds a complete HTTP message.
    -   Body class holds the HTTP content body.
    -   Headers class holds RFC-compliant HTTP headers.
    -   Basic\_parser provides class interface to joyent's http-parser.
    -   Parser class parses into a message object.
    -   Remove unused http get client free function.
    -   Unit test for parsing malformed messages.
-   Add enable\_if\_lvalue.
-   Updates to includes and scons.
-   Additional ledger.history.mismatch insight statistic.
-   Convert rvalue to an lvalue. ([RIPD-494](https://ripplelabs.atlassian.net/browse/RIPD-494))
-   Enable heap profiling with jemalloc.
-   Add aged containers to Validators module. ([RIPD-349](https://ripplelabs.atlassian.net/browse/RIPD-349))
-   Account for high-ASCII characters. ([RIPD-464](https://ripplelabs.atlassian.net/browse/RIPD-464))

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.26.2

rippled 0.26.2 has been released. The repository tag is *0.26.2* and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit b9454e0f0ca8dbc23844a0520d49394e10d445b1
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Aug 11 15:25:44 2014 -0400

        Set version to 0.26.2

This release incorporates a small number of important bugfixes. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.2) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**New Features**

-   Freeze enforcement: activates on September 15, 2014 ([RIPD-399](https://ripplelabs.atlassian.net/browse/RIPD-399))
-   Add pubkey\_node and hostid to server stream messages ([RIPD-407](https://ripplelabs.atlassian.net/browse/RIPD-407))

**Bug Fixes**

-   Fix intermittent exception when closing HTTPS connections ([RIPD-475](https://ripplelabs.atlassian.net/browse/RIPD-475))
-   Correct Pathfinder::getPaths out to handle order books ([RIPD-427](https://ripplelabs.atlassian.net/browse/RIPD-427))
-   Detect inconsistency in PeerFinder self-connects ([RIPD-411](https://ripplelabs.atlassian.net/browse/RIPD-411))

**Experimental Features**

-   Add owner\_funds to client subscription data ([RIPD-377](https://ripplelabs.atlassian.net/browse/RIPD-377))

The offer funding status feature is “experimental” in this version. Developers are able to see the field, but it is subject to change in future releases.

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.26.1

rippled v0.26.1 has been released. The repository tag is **0.26.1** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.1>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 9a0e806f78300374e20070e2573755fbafdbfd03
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon Jul 28 11:27:31 2014 -0700

         Set version to 0.26.1

This release incorporates a small number of important bugfixes. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/0.26.1) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Bug Fixes**

-   Enabled asynchronous handling of HTTP-RPC interactions. This fixes client handlers using RPC that periodically return blank responses to requests. ([RIPD-390](https://ripplelabs.atlassian.net/browse/RIPD-390))
-   Fixed auth handling during OfferCreate. This fixes a regression of [RIPD-256](https://ripplelabs.atlassian.net/browse/RIPD-256). ([RIPD-414](https://ripplelabs.atlassian.net/browse/RIPD-414))

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.26.0

rippled v0.26.0 has been released. The repository tag is **0.26.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.26.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 9fa5e3987260e39dba322f218d39ac228a5b361b
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Tue Jul 22 09:59:45 2014 -0700

         Set version to 0.26.0

This release incorporates a significant number of improvements and important bugfixes. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more detailed information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend compiling on (virtual) machines with 8GB of RAM or more. If your build machine has more than one CPU (**\`grep '^processor' /proc/cpuinfo | wc -l\`**), you can use them to assist in the build process by compiling with the command **scons -j\[number of CPUs - 1\]**.

The minimum supported version of Boost is v1.55.0. You **must** upgrade to this release or later to successfully compile this release of rippled. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   Updated integration tests.
-   Updated tests for account freeze functionality.
-   Implement setting the no-freeze flag on Ripple accounts ([RIPD-394](https://ripplelabs.atlassian.net/browse/RIPD-394)).
-   Improve transaction fee and execution logic ([RIPD-323](https://ripplelabs.atlassian.net/browse/RIPD-323)).
-   Implemented finding of 'sabfd' paths ([RIPD-335](https://ripplelabs.atlassian.net/browse/RIPD-335)).
-   Imposed a local limit on paths lengths ([RIPD-350](https://ripplelabs.atlassian.net/browse/RIPD-350)).
-   Documented [ledger entries](https://github.com/ripple/rippled/blob/develop/src/ripple/module/app/ledger/README.md) ([RIPD-361](https://ripplelabs.atlassian.net/browse/RIPD-361)).
-   Documented [SHAMap](https://github.com/ripple/rippled/blob/develop/src/ripple/module/app/shamap/README.md).

**Bug Fixes**

-   Fixed the limit parameter on book\_offers ([RIPD-295](https://ripplelabs.atlassian.net/browse/RIPD-295)).
-   Removed SHAMapNodeID from SHAMapTreeNode to fix "right data, wrong ID" bug in the tree node cache ([RIPD-347](https://ripplelabs.atlassian.net/browse/RIPD-347)).
-   Eliminated spurious SHAMap::getFetchPack failure ([RIPD-379](https://ripplelabs.atlassian.net/browse/RIPD-379)).
-   Disabled SSLv2.
-   Implemented rate-limiting of SSL client renegotiation to mitigate [SCIR DoS vulnerability](https://www.thc.org/thc-ssl-dos/) ([RIPD-360](https://ripplelabs.atlassian.net/browse/RIPD-360)).
-   Display unprintable or malformatted currency codes as hex digits.
-   Fix static initializers in RippleSSLContext ([RIPD-375](https://ripplelabs.atlassian.net/browse/RIPD-375)).

**More information**

For more information or assistance, the following resources will be of use:

-   [Ripple Developer Forums](https://ripple.com/forum/viewforum.php?f=2)
-   [IRC](https://webchat.freenode.net/?channels=#ripple)


-----------------------------------------------------------

## Version 0.25.2

rippled v0.25.2 has been released. The repository tag is **0.25.2** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.25.2>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit ddf68d464d74e1c76a0cfd100a08bc8e65b91fec
     Author: Mark Travis <mtravis@ripple.com>
     Date:   Mon Jul 7 11:46:15 2014 -0700

         Set version to 0.25.2

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

While it may be possible to compile rippled on (virtual) machines with 4GB of RAM, we recommend build machines with 8GB of RAM.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   CPU utilization for certain operations has been optimized.
-   Improve serialization of public ledger blocks.
-   rippled now takes much less time to compile.
-   Additional pathfinding heuristic: increases liquidity in some cases.

**Bug Fixes**

-   Unprintable currency codes will be printed as hex digits.
-   Transactions with unreasonably long path lengths are rejected. The maximum is now eight (8) hops.


-----------------------------------------------------------

## Version 0.25.1

`rippled` v0.25.1 has been released. The repository tag is `0.25.1` and can be found on GitHub at: https://github.com/ripple/rippled/tree/0.25.1

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

     commit b677cacb8ce0d4ef21f8c60112af1db51dce5bb4
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Thu May 15 08:27:20 2014 -0700

         Set version to 0.25.1

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines.  Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8.  Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55.  You **must** upgrade to this release or later to successfully compile this release.  Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Major Features**

* Option to compress the NodeStore db. More speed, less space. See [`rippled-example.cfg`](https://github.com/ripple/rippled/blob/0.25.1/doc/rippled-example.cfg#L691)

**Improvements**

* Remove redundant checkAccept call
* Added I/O latency to output of ''server_info''.
* Better performance handling of Fetch Packs.
* Improved handling of modified ledger nodes.
* Improved performance of JSON document generator.
* Made strConcat operate in O(n) time for greater efficiency.
* Added some new configuration options to doc/rippled-example.cfg

**Bug Fixes**

* Fixed a bug in Unicode parsing of transactions.
* Fix a blocker with tfRequireAuth
* Merkle tree nodes that are retrieved as a result of client requests are cached locally.
* Use the last ledger node closed for finding old paths through the network.
* Reduced number of asynchronous fetches.


-----------------------------------------------------------

## Version 0.25.0

rippled version 0.25.0 has been released. The repository tag is **0.25.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.25.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 29d1d5f06261a93c5e94b4011c7675ff42443b7f
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Wed May 14 09:01:44 2014 -0700

         Set version to 0.25.0

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Major Features**

-   Option to compress the NodeStore db. More speed, less space. See [`rippled-example.cfg`](https://github.com/ripple/rippled/blob/0.25.0/doc/rippled-example.cfg#L691)

**Improvements**

-   Remove redundant checkAccept call
-   Added I/O latency to output of *server\_info*.
-   Better performance handling of Fetch Packs.
-   Improved handling of modified ledger nodes.
-   Improved performance of JSON document generator.
-   Made strConcat operate in O(n) time for greater efficiency.

**Bug Fixes**

-   Fix a blocker with tfRequireAuth
-   Merkle tree nodes that are retrieved as a result of client requests are cached locally.
-   Use the last ledger node closed for finding old paths through the network.
-   Reduced number of asynchronous fetches.


-----------------------------------------------------------

## Version 0.24.0

rippled version 0.24.0 has been released. The repository tag is **0.24.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.24.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 3eb1c7bd6f93e5d874192197f76571184338f702
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Mon May 5 10:20:46 2014 -0700

         Set version to 0.24.0

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   Implemented logic for ledger processes and features.
-   Use "high threads" for background RocksDB database writes.
-   Separately track locally-issued transactions to ensure they always appear in the open ledger.

**Bug Fixes**

-   Fix AccountSet for canonical transactions.
-   The RPC [sign](https://ripple.com/build/rippled-apis/#sign) command will now sign with either an account's master or regular secret key.
-   Fixed out-of-order network initialization.
-   Improved efficiency of pathfinding for transactions.
-   Reworked timing of ledger validation and related operations to fix race condition against the network.
-   Build process enforces minimum versions of OpenSSL and BOOST for operation.


-----------------------------------------------------------

## Version 0.23.0

rippled version 0.23.0 has been released. The repository tag is **0.23.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.23.0>

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

     commit 29a4f61551236f70865d46d6653da2e62de1c701
     Author: Vinnie Falco <vinnie.falco@gmail.com>
     Date:   Fri Mar 14 13:01:23 2014 -0700

         Set version to 0.23.0

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of Boost is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Improvements**

-   Allow the word 'none' in the *.cfg* file to disable storing historical ledgers.
-   Clarify the initialization of hash prefixes used in the *RadMap*.
-   Better validation of RPC-JSON from all sources
-   Reduce spurious log output from Peers
-   Eliminated some I/O for certain operations in the *RadMap*.
-   Client requests for full state trees now require administrative privileges.
-   Added "MemoData" field for transaction memos.
-   Prevent the node cache from overflowing needlessly in certain cases
-   Add "ledger\_data" command for retrieving entire ledgers in chunks.
-   Reduce the quantity of forwarded transactions and proposals in some cases
-   Improved diagnostics when errors occur loading SSL certificates

**Bug Fixes**

-   Fix rare crash when a race condition involving disconnecting sockets occurs
-   Fix a corner case with hex conversion of strings with odd character lengths
-   Fix an exception in a corner case when erroneous transactions were being logged
-   Fix the treatment of expired offers when cleaning up offers
-   Prevent a needless transactor from being created if the tx ID is not valid
-   Fix the peer action transition from "syncing" to "full"
-   Fix error reporting for unknown inner JSON fields
-   Fix source file path displayed when an assertion failure is reported
-   Fix typos in transaction engine error code identifiers


-----------------------------------------------------------

## Version 0.22.0

rippled version 0.22.0 has been released. This release is currently the tip of the **develop/** branch and can be found on GitHub at: <https://github.com/ripple/rippled/tree/develop> The tag is **0.22.0** and can be found on GitHub at: <https://github.com/ripple/rippled/tree/0.22.0>

**This is a critical release affecting transaction processing. All partners should update immediately.**

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

This release incorporates significant improvements which may not warrant separate entries but are incorporated into the feature changes as summary lines. Please refer to the [Git commit history](https://github.com/ripple/rippled/commits/develop) for more information.

**Toolchain support**

The minimum supported version of GCC used to compile rippled is v4.8. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Ubuntu_versions_older_than_13.10_:_Install_gcc_4.8) if you have not upgraded already.

A minimum of 4GB of RAM are required to successfully compile this release.

The minimum supported version of libBOOST is v1.55. You **must** upgrade to this release or later to successfully compile this release. Please follow [these instructions](https://wiki.ripple.com/Ubuntu_build_instructions#Install_Boost) if you have not upgraded already.

**Key release features**

- **PeerFinder**

    -   Actively guides network topology.
    -   Scrubs listening advertisements based on connectivity checks.
    -   Redirection for new nodes when existing nodes are full.

- **Memos**

    -   Transactions can optionally include a short text message, which optionally can be encrypted.

- **Database**

    -   Improved management of I/O resources.
    -   Better performance accessing historical data.

- **PathFinding**

    -   More efficient search algorithm when computing paths

**Major Partner Issues Fixed**

- **Transactions**

    -   Malleability: Ability to ensure that signatures are fully canonical.

- **PathFinding**

    -   Less time needed to get the first path result!

- **Database**

    -   Eliminated "meltdowns" caused when fetching historical ledger data.

**Significant Changes**

-   Cleaned up logic which controls when ledgers are fetched and under what conditions.
-   Cleaned up file path calculation for database files.
-   Changed dispatcher for WebSocket requests.
-   Cleaned up multithreading mechanisms.
-   Fixed custom currency code parsing.
-   Optimized transaction node lookup circumstances in the node store.


-----------------------------------------------------------

## Version 0.21.0

rippled version 0.21.0 has been released. This release is currently the tip of the **develop/** branch and can be found on GitHub at [1](https://github.com/ripple/rippled/tree/develop). The tag is **0.21.0-rc2** and can be found on GitHub at [2](https://github.com/ripple/rippled/tree/0.21.0-rc2).

**This is a critical release. All partners should update immediately.**

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

    commit f295bb20a16d1d2999f606c1297c8930d8e33c40
    Author: JoelKatz <DavidJoelSchwartz@GMail.com>
    Date:   Fri Jan 24 11:17:16 2014 -0800

        Set version to 0.21.0.rc2

**Major Partner Issues Fixed**

-   Order book issues
    -   Ensure all crossing offers are taken
    -   Ensure order book is not left crossed
-   Added **DeliveredAmount** field to transaction metadata
    -   Reports amount delivered in partial payments

**Toolchain support**

As with the previous release, the minimum supported version of GCC used to compile rippled is v4.8.

**Significant Changes**

-   Pairwise no-ripple
    -   Permits trust lines to be protected from rippling
    -   Operates on protected pairs
-   Performance improvements
    -   Improve I/O latency
    -   Improve fetching ledgers
    -   Improve pathfinding
-   Features for robust transaction submission
    -   LastLedgerSeq for transaction expiration
    -   AccountTxnID for transaction chaining
-   Fix some cases where an invalid transaction would stay in limbo
-   Code cleanups
-   Better reporting of invalid parameters

**Release Candidates**

RC1 fixed performance problems with order book retrieval.

RC2 fixed a bug that caused crashes in order processing and a bug in parsing order book requests.

**Notice**

If you are upgrading from version 0.12 or earlier of rippled, these next sections apply to you because the format of the *rippled.cfg* file changed around that time. If you have upgraded since that time and you have applied the configuration file fixes, you can safely ignore them.

**Validators**

Ripple Labs is now running five validators. You can use this template for your *validators.txt* file (or place this in your config file):

     [validators]
     n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
     n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
     n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
     n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
     n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your *rippled.cfg* file:

     [validation_quorum]
     3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving **r.ripple.com**. You can also add this to your *rippled.cfg* file to ensure you always have several peer connections to Ripple Labs servers:

     [ips]
     184.73.226.101 51235
     23.23.201.55   51235
     54.200.43.173  51235
     184.73.57.84   51235
     54.234.249.55  51235
     54.200.86.110  51235

**RocksDB back end**

RocksDB is based on LevelDB with improvements from Facebook and the community. Preliminary tests show that it stalls less often than HyperLevelDB for our use cases.

If you are switching over from an existing back end, you have two options. You can remove your old database and let rippled recreate it as it re-syncs, or you can import your old database into the new one.

To remove your old database, make sure the server is shut down (\`rippled stop\`). Remove the *db/ledger.db* and *db/transaction.db* files. Remove all the files in your back end store directory (*db/hashnode* by default). Then change your configuration file to use the RocksDB back end and restart.

To import your old database, start by shutting the server down. Then modify the configuration file by renaming your *\[node\_db\]* stanza to *\[import\_db\]*. Create a new *\[node\_db\]* stanza and specify a RocksDB back end with a different directory. Start the server with the command **rippled --import**. When the import finishes gracefully stop the server (\`rippled stop\`). Please wait for rippled to stop on its own because it can take several minutes for it to shut down after an import. Remove the old database, put the new database into place, remove the *\[import\_db\]* section, change the *\[node\_db\]* section to refer to the final location, and restart the server.

The recommended RocksDB configuration is:

     [node_db]
     type=RocksDB
     path=db/hashnode
     open_files=1200
     filter_bits=12
     cache_mb=128
     file_size_mb=8
     file_size_mult=2

**Configuring your Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. See above for an example RocksDB configuration.

-   **Note**: HyperLevelDB and RocksDB are not available on Windows platform.


-----------------------------------------------------------

## Version 0.20.1

rippled version 0.20.1 has been released. This release is currently the tip of the [develop](https://github.com/ripple/rippled/tree/develop) branch and the tag is [0.20.1](https://github.com/ripple/rippled/tree/0.20.1).

**This is a critical release. All partners should update immediately.**

Prior to building, please confirm you have the correct source tree with the **git log** command. The first log entry should be the change setting the version:

    commit 95a573b755219d7e1e078d53b8e11a8f0d7cade1
    Author: Vinnie Falco <vinnie.falco@gmail.com>
    Date:   Wed Jan 8 17:08:27 2014 -0800

       Set version to 0.20.1

**Major Partner Issues Fixed**

-   rippled will crash randomly.
    -   Entries in the three parts of the order book are missing or do not match. In such a case, rippled will crash.
-   Server loses sync randomly.
    -   This is due to rippled restarting after it crashes. That the server restarted is not obvious and appears to be something else.
-   Server goes 'offline' randomly.
    -   This is due to rippled restarting after it crashes. That the server restarted is not obvious and appears to be something else.
-   **complete\_ledgers** part of **server\_info** output says "None".
    -   This is due to rippled restarting and reconstructing the ledger after it crashes.
    -   If the node back end is corrupted or has been moved without being renamed in rippled.cfg, this can cause rippled to crash and restart.

**Toolchain support**

Starting with this release, the minimum supported version of GCC used to compile rippled is v4.8.

**Significant Changes**

-   Don't log StatsD messages to the console by default.
-   Fixed missing jtACCEPT job limit.
-   Removed dead code to clean up the codebase.
-   Reset liquidity before retrying rippleCalc.
-   Made improvements becuase items in SHAMaps are immutable.
-   Multiple pathfinding bugfixes:
    -   Make each path request track whether it needs updating.
    -   Improve new request handling, reverse order for processing requests.
    -   Break to handle new requests immediately.
    -   Make mPathFindThread an integer rather than a bool. Allow two threads.
    -   Suspend processing requests if server is backed up.
    -   Multiple performance improvements and enhancements.
    -   Fixed locking.
-   Refactored codebase to make it C++11 compliant.
-   Multiple fixes to ledger acquisition, cleanup, and logging.
-   Made multiple improvements to WebSockets server.
-   Added Debian-style initscript (doc/rippled.init).
-   Updated default config file (doc/rippled-example.cfg) to reflect best practices.
-   Made changes to SHAMapTreeNode and visitLeavesInternal to conserve memory.
-   Implemented new fee schedule:
    -   Transaction fee: 10 drops
    -   Base reserve: 20 XRP
    -   Incremental reserve: 5 XRP
-   Fixed bug \#211 (getTxsAccountB in NetworkOPs).
-   Fixed a store/fetch race condition in ther node back end.
-   Fixed multiple comparison operations.
-   Removed Sophia and Lightning databases.

**Notice**

If you are upgrading from version 0.12 or earlier of rippled, these next sections apply to you because the format of the *rippled.cfg* file changed around that time. If you have upgraded since that time and you have applied the configuration file fixes, you can safely ignore them.

**Validators**

Ripple Labs is now running five validators. You can use this template for your *validators.txt* file (or place this in your config file):

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your *rippled.cfg* file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving **r.ripple.com**. You can also add this to your *rippled.cfg* file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**New RocksDB back end**

RocksDB is based on LevelDB with improvements from Facebook and the community. Preliminary tests show that it stalls less often than HyperLevelDB for our use cases.

If you are switching over from an existing back end, you have two options. You can remove your old database and let rippled recreate it as it re-syncs, or you can import your old database into the new one.

To remove your old database, make sure the server is shut down (`rippled stop`). Remove the *db/ledger.db* and *db/transaction.db* files. Remove all the files in your back end store directory (*db/hashnode* by default). Then change your configuration file to use the RocksDB back end and restart.

To import your old database, start by shutting the server down. Then modify the configuration file by renaming your *\[node\_db\]* stanza to *\[import\_db\]*. Create a new *\[node\_db\]* stanza and specify a RocksDB back end with a different directory. Start the server with the command **rippled --import**. When the import finishes gracefully stop the server (`rippled stop`). Please wait for rippled to stop on its own because it can take several minutes for it to shut down after an import. Remove the old database, put the new database into place, remove the *\[import\_db\]* section, change the *\[node\_db\]* section to refer to the final location, and restart the server.

The recommended RocksDB configuration is:

    [node_db]
    type=RocksDB
    path=db/hashnode
    open_files=1200
    filter_bits=12
    cache_mb=256
    file_size_mb=8
    file_size_mult=2

**Configuring your Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. See above for an example RocksDB configuration.

-   **Note**: HyperLevelDB and RocksDB are not available on Windows platform.


-----------------------------------------------------------

## Version 0.19

rippled version 0.19 has now been released. This release is currently the tip of the [release](https://github.com/ripple/rippled/tree/release) branch and the tag is [0.19.0](https://github.com/ripple/rippled/tree/0.19.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit 26783607157a8b96e6e754f71565f4eb0134efc1
    Author: Vinnie Falco <vinnie.falco@gmail.com>
    Date:   Fri Nov 22 23:36:50 2013 -0800

        Set version to 0.19.0

**Significant Changes**

-   Bugfixes and improvements in path finding, path filtering, and payment execution.
-   Updates to HyperLevelDB and LevelDB node storage back ends.
-   Addition of RocksDB node storage back end.
-   New resource manager for tracking server load.
-   Fixes for a few bugs that can crashes or inability to serve client requests.

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file (or place this in your config file):

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**New RocksDB back end**

RocksDB is based on LevelDB with improvements from Facebook and the community. Preliminary tests show that it stall less often than HyperLevelDB.

If you are switching over from an existing back end, you have two choices. You can remove your old database or you can import it.

To remove your old database, make sure the server is shutdown. Remove the `db/ledger.db` and `db/transaction.db` files. Remove all the files in your back end store directory, `db/hashnode` by default. Then you can change your configuration file to use the RocksDB back end and restart.

To import your old database, start by shutting the server down. Then modify the configuration file by renaming your `[node_db]` portion to `[import_db]`. Create a new `[node_db]` section specify a RocksDB back end and a different directory. Start the server with `rippled --import`. When the import finishes, stop the server (it can take several minutes to shut down after an import), remove the old database, put the new database into place, remove the `[import_db]` section, change the `[node_db]` section to refer to the final location, and restart the server.

The recommended RocksDB configuration is:

    [node_db]
    type=RocksDB
    path=db/hashnode
    open_files=1200
    filter_bits=12
    cache_mb=256
    file_size_mb=8
    file_size_mult=2

**Configuring your Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. See above for an example RocksDB configuration.

-   **Note:** HyperLevelDB and RocksDB are not available on Windows platform.


-----------------------------------------------------------

## Version 0.16

rippled version 0.16 has now been released. This release is currently the tip of the [master](https://github.com/ripple/rippled/tree/master) branch and the tag is [v0.16.0](https://github.com/ripple/rippled/tree/v0.16.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit 15ef43505473225af21bb7b575fb0b628d5e7f73
    Author: vinniefalco
    Date:   Wed Oct 2 2013

       Set version to 0.16.0

**Significant Changes**

-   Improved peer discovery
-   Improved pathfinding
-   Ledger speed improvements
-   Reduced memory consumption
-   Improved server stability
-   rippled no longer throws and exception on exiting
-   Better error reporting
-   Ripple-lib tests have been ported to use the Mocha testing framework

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file:

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. In most cases, that will mean adding this to your configuration file:

    [node_db]
    type=HyperLevelDB
    path=db/hashnode

-   NOTE HyperLevelDB is not available on Windows platforms.

**Release Candidates**

**Issues**

None known


-----------------------------------------------------------

## Version 0.14

rippled version 0.14 has now been released. This release is currently the tip of the [master](https://github.com/ripple/rippled/tree/master) branch and the tag is [v0.12.0](https://github.com/ripple/rippled/tree/v0.14.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit b6d11c08d0245ee9bafbb97143f5d685dd2979fc
    Author: vinniefalco
    Date:   Wed Oct 2 2013

       Set version to 0.14.0

**Significant Changes**

-   Improved peer discovery
-   Improved pathfinding
-   Ledger speed improvements
-   Reduced memory consumption
-   Improved server stability
-   rippled no longer throws and exception on exiting
-   Better error reporting
-   Ripple-lib tests have been ported to use the Mocha testing framework

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file:

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. In most cases, that will mean adding this to your configuration file:

    [node_db]
    type=HyperLevelDB
    path=db/hashnode

-   NOTE HyperLevelDB is not available on Windows platforms.

**Release Candidates**

**Issues**

None known


-----------------------------------------------------------

## Version 0.12

rippled version 0.12 has now been released. This release is currently the tip of the [master branch](https://github.com/ripple/rippled/tree/master) and can be found on GitHub. The tag is [v0.12.0](https://github.com/ripple/rippled/tree/v0.12.0).

Prior to building, please confirm you have the correct source tree with the `git log` command. The first log entry should be the change setting the version:

    commit d0a9da6f16f4083993e4b6c5728777ffebf80f3a
    Author: JoelKatz <DavidJoelSchwartz@GMail.com>
    Date:   Mon Aug 26 12:08:05 2013 -0700

        Set version to v0.12.0

**Major Partner Issues Fixed**

-   Server Showing "Offline"

This issue was caused by LevelDB periodically compacting its internal data structure. While compacting, rippled's processing would stall causing the node to lose sync with the rest of the network. This issue was solved by switching from LevelDB to HyperLevelDB. rippled operators will need to change their ripple.cfg file. See below for configuration details.

-   Premature Validation of Transactions

On rare occasions, a transaction would show as locally validated before the full network consensus was confirmed. This issue was resolved by changing the way transactions are saved.

-   Missing Ledgers

Occasionally, some rippled servers would fail to fetch all ledgers. This left gaps in the local history and caused some API calls to report incomplete results. The ledger fetch code was rewritten to both prevent this and to repair any existing gaps.

**Significant Changes**

-   The way transactions are saved has been changed. This fixes a number of ways transactions can incorrectly be reported as fully-validated.
-   `doTransactionEntry` now works against open ledgers.
-   `doLedgerEntry` now supports a binary option.
-   A bug in `getBookPage` that caused it to skip offers is fixed.
-   `getNodeFat` now returns deeper chains, reducing ledger acquire latency.
-   Catching up if the (published ledger stream falls behind the network) is now more aggressive.
-   I/O stalls are drastically reduced by using the HyperLevelDB node back end.
-   Persistent ledger gaps should no longer occur.
-   Clusters now exchange load information.

**Validators**

Ripple Labs is now running five validators. You can use this template for your `validators.txt` file:

<strike>

    [validators]
    n9KPnVLn7ewVzHvn218DcEYsnWLzKerTDwhpofhk4Ym1RUq4TeGw    RIP1
    n9LFzWuhKNvXStHAuemfRKFVECLApowncMAM5chSCL9R5ECHGN4V    RIP2
    n94rSdgTyBNGvYg8pZXGuNt59Y5bGAZGxbxyvjDaqD9ceRAgD85P    RIP3
    n9LeQeDcLDMZKjx1TZtrXoLBLo5q1bR1sUQrWG7tEADFU6R27UBp    RIP4
    n9KF6RpvktjNs2MDBkmxpJbup4BKrKeMKDXPhaXkq7cKTwLmWkFr    RIP5

</strike>

**Update April 2014** - Due to a vulnerability in OpenSSL the validator keys above have been cycled out, the five validators by RippleLabs use the following keys now:

    [validators]
    n949f75evCHwgyP4fPVgaHqNHxUVN15PsJEZ3B3HnXPcPjcZAoy7     RL1
    n9MD5h24qrQqiyBC8aeqqCWvpiBiYQ3jxSr91uiDvmrkyHRdYLUj     RL2
    n9L81uNCaPgtUJfaHh89gmdvXKAmSt5Gdsw2g1iPWaPkAHW5Nm4C     RL3
    n9KiYM9CgngLvtRCQHZwgC2gjpdaZcCcbt3VboxiNFcKuwFVujzS     RL4
    n9LdgEtkmGB9E2h3K4Vp7iGUaKuq23Zr32ehxiU8FWY7xoxbWTSA     RL5

You should also raise your quorum to at least three by putting the following in your `rippled.cfg` file:

    [validation_quorum]
    3

If you are a validator, you should set your quorum to at least four.

**IPs**

A list of Ripple Labs server IP addresses can be found by resolving `r.ripple.com`. You can also add this to your `rippled.cfg` file to ensure you always have several peer connections to Ripple Labs servers:

    [ips]
    54.225.112.220 51235
    54.225.123.13  51235
    54.227.239.106 51235
    107.21.251.218 51235
    184.73.226.101 51235
    23.23.201.55   51235

**Node DB**

You need to configure the [NodeBackEnd](https://wiki.ripple.com/NodeBackEnd) that you want the server to use. In most cases, that will mean adding this to your configuration file:

    [node_db]
    type=HyperLevelDB
    path=db/hashnode

-   NOTE HyperLevelDB is not available on Windows platforms.

**Release Candidates**

RC1 was the first release candidate.

RC2 fixed a bug that could cause ledger acquires to stall.

RC3 fixed compilation under OSX.

RC4 includes performance improvements in countAccountTx and numerous small fixes to ledger acquisition.

RC5 changed the peer low water mark from 4 to 10 to acquire more server connections.

RC6 fixed some possible load issues with the network state timer and cluster reporting timers.

**Issues**

Fetching of historical ledgers is slower in this build than in previous builds. This is being investigated.
