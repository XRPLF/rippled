# `AccountObjects.cpp` — `account_objects` RPC Handler

This file implements the `account_objects` RPC command, which returns all ledger objects owned by a specific XRPL account. It is one of the most structurally complex account-query handlers because the XRP Ledger stores two fundamentally different kinds of account-owned objects in two separate data structures: the **owner directory** (a linked list of `ltDIR_NODE` pages) and **NFT pages** (`ltNFTOKEN_PAGE`). Bridging those two structures behind a single paginated API is the central challenge the file solves.

## Dual-Structure Traversal in `getAccountObjects`

The workhorse function `getAccountObjects` must traverse two disjoint ledger structures in sequence. NFT pages are stored as a sorted range of `ltNFTOKEN_PAGE` entries whose keys are derived from the account ID (upper 160 bits) plus a 96-bit token sort value. They are _not_ listed in the owner directory. Every other owned object — offers, trust lines, escrows, payment channels, checks, and so on — lives in the owner directory.

The function handles this by iterating NFT pages first, then pivoting to the owner directory. The boolean `iterateNFTPages` encodes the conditions under which NFT pages should be included: the type filter must not exclude `ltNFTOKEN_PAGE`, and — critically — `dirIndex` must be zero. A non-zero `dirIndex` in a resumed-pagination call means the client is resuming mid-owner-directory, which implies all NFT pages were already returned in a prior response. This avoids redundant re-emission of NFT data on resume.

The NFT page iteration uses `ledger.succ()` to find the first extant page within `[nftpage_min(account), nftpage_max(account)]`, then follows the `sfNextPageMin` field on each page as a linked-list pointer to the next page. This chain traversal is inherently forward-only and safe: if the page doesn't exist, the loop terminates.

## The Marker Protocol and Its Edge Cases

Pagination is expressed as a marker string `"<dirIndex>,<entryIndex>"` where both components are hex-encoded 256-bit values. The choice to use a comma-separated pair — rather than a single opaque cursor — is deliberate: it lets the handler determine which phase (NFT or directory) a resumed request belongs to by inspecting `dirIndex`. A zero `dirIndex` combined with a non-zero `entryIndex` that matches `firstNFTPage.key == (entryIndex & ~nft::pageMask)` signals a resume mid-NFT-page-chain.

The bitmask `~nft::pageMask` strips the lower 96 bits of `entryIndex` (the token-sort portion) leaving only the upper 160-bit account prefix. If that prefix equals `firstNFTPage.key`, the marker encodes an NFT page key for this account; otherwise `iterateNFTPages` is cleared and the marker is treated as a directory entry.

A subtle edge case arises when NFT pages fill the response exactly to the `limit`. At that boundary the code emits a marker of the form `"0,<last_nft_page_key>"` — `dirIndex` is literally the string "0" — so the next call resumes from that NFT page position. A second boundary exists when the NFT phase ends and the directory phase immediately hits `mlimit` before consuming a single entry: the code at line 167–172 emits a directory-style marker using the first entry of the current directory node even though `i == 0`, preventing a lost-entry scenario.

`mlimit` (a mutable copy of `limit`) tracks remaining capacity uniformly across both phases so that the two loops share a single budget without needing to communicate remaining capacity through a return value.

## Request Handling in `doAccountObjects`

`doAccountObjects` validates the incoming JSON request and dispatches to `getAccountObjects`. The two most architecturally interesting decisions here are the `deletion_blockers_only` mode and the type validation gate.

**`deletion_blockers_only`** constructs a compile-time table of `{jss::name, LedgerEntryType}` pairs covering every ledger entry type that prevents account deletion: `ltCHECK`, `ltESCROW`, `ltNFTOKEN_PAGE`, `ltPAYCHAN`, `ltRIPPLE_STATE`, cross-chain objects (`ltXCHAIN_OWNED_CLAIM_ID`, `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID`, `ltBRIDGE`), and newer token types (`ltMPTOKEN_ISSUANCE`, `ltMPTOKEN`, `ltPERMISSIONED_DOMAIN`, `ltVAULT`). When a `type` parameter is also present alongside `deletion_blockers_only`, the table is further filtered to the intersection. This mode lets callers quickly audit which objects block account removal without a full scan.

**Type validation** via `isAccountObjectsValidType` rejects a small set of global ledger state types (`ltAMENDMENTS`, `ltDIR_NODE`, `ltFEE_SETTINGS`, `ltLEDGER_HASHES`, `ltNEGATIVE_UNL`) that are ledger-global rather than account-owned. Requesting these would either return nothing or produce confusing results, so the handler surfaces a clean `rpcINVALID_PARAMS` instead.

## Resource and Error Handling

The handler is tagged `feeMediumBurdenRPC`, reflecting that a full account-objects scan can require many ledger reads across chained directory nodes and NFT pages. The pagination limit is enforced via `RPC::Tuning::accountObjects` (min 10, default 200, max 400), parsed through `readLimitField` which handles clamping automatically.

`getAccountObjects` returns `false` only in one specific case: when `entryIndex` is non-zero and cannot be located in the expected directory node. This maps directly to `rpcINVALID_PARAMS` for `jss::marker` at the call site, giving the client a precise signal that their marker is stale or corrupted. All other early exits — missing directory, no objects — return `true` with an empty `account_objects` array, distinguishing "nothing to return" from "your marker was invalid."