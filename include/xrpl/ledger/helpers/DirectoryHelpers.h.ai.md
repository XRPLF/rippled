# `DirectoryHelpers.h` — Ledger Directory Traversal Utilities

## Role in the System

In the XRPL ledger, a *directory* (`ltDIR_NODE`) is the data structure through which sets of related ledger objects are tracked. Every account's owned objects — offers, trust lines, payment channels, tickets, and more — are registered in that account's *owner directory*. Order book listings live in their own directory trees. A directory is physically a linked list of pages: each page (`SLE` of type `ltDIR_NODE`) holds an `sfIndexes` field, a `STVector256` of ledger-entry keys, and an `sfIndexNext` field that chains to the next page.

`DirectoryHelpers.h` provides all the machinery for walking these multi-page directories: a low-level legacy cursor API, higher-level callback-based iterators, an emptiness check, and a factory helper used when inserting new directory nodes.

## Const-Aware Template Core

The real implementation lives in two private templates under `namespace xrpl::detail`: `internalDirFirst` and `internalDirNext`. Both are constrained with `std::enable_if_t` so they only instantiate for a view type that inherits from `ReadView` and a node type that is exactly `SLE` or `SLE const`.

The critical design choice is the `if constexpr (std::is_const_v<N>)` branch inside both templates. When `N` is `SLE const`, the template calls `view.read(...)` to obtain a const smart pointer — appropriate for read-only traversal via `ReadView`. When `N` is `SLE`, it calls `view.peek(...)` to get a mutable smart pointer — required when transactors need to modify the SLE while iterating (possible through `ApplyView`). This single template body unifies the read and write paths without duplicating logic or requiring virtual dispatch.

## The Legacy Cursor API

The four public functions `cdirFirst`, `cdirNext`, `dirFirst`, and `dirNext` are thin wrappers that route to the private templates. They maintain traversal state entirely in the caller: a `shared_ptr<SLE [const]>` for the current page, an `unsigned int` cursor within that page, and a `uint256` that receives the current entry's key. The caller is responsible for holding all three between calls.

These functions are marked deprecated in favour of an iterator-based model. Their continued existence is justified by one specific use case: deletion while iterating. In `cleanupOnAccountDelete` (in `View.cpp`) the code iterates with `dirFirst`/`dirNext` while deleting ledger entries underneath. Because deletion shifts remaining entries in the page, the code manually decrements `uDirEntry` after each deletion to keep the cursor aligned. A clean C++ iterator would be invalidated immediately and could not be repaired this way; the exposed-state design allows the caller to patch the cursor, trading abstraction for correctness in this narrow case. The source comment in `View.cpp` (line 485–509) documents this technique explicitly.

`internalDirNext` handles page transitions transparently: when the cursor reaches the end of `sfIndexes`, it reads `sfIndexNext` from the current page and follows the chain. If `sfIndexNext` is zero the directory is exhausted. The function then tail-calls itself to advance into the new page's first entry in one logical step.

## Higher-Level Callback Iterators

`forEachItem(ReadView, Keylet, f)` walks every entry in a directory by iterating pages in a simple `while(true)` loop, calling `view.read(keylet::child(key))` to materialise each entry and passing the result to the callback `f`. It never exposes the page or index cursor, making it safe for read-only scanning.

`forEachItemAfter` supports cursor-based pagination as needed by RPC handlers (`account_offers`, `account_lines`, `account_channels`). It accepts an `after` key and a `hint` page number. The hint represents the directory page last seen by the client — the implementation first jumps to that page and checks whether `after` appears in it. If found, traversal resumes from that page rather than page 0, reducing the O(n) cost of re-scanning all prior pages on each paginated request. If the hint doesn't pan out, the code falls back to a linear scan from the root. The callback `f` returns a `bool`; returning `true` signals "stop" and counts against the supplied `limit`, letting callers cap how many items they process in a single RPC call. The function returns `false` if the directory itself is missing or `after` was never found, which callers treat as a pagination error.

The two `inline` overloads that accept `AccountID` simply call `keylet::ownerDir(id)` and forward to the `Keylet` versions, providing a convenience entry point for the common case of iterating an account's owner directory.

## `dirIsEmpty`

A subtlety of the directory structure is that the root page (the anchor page) can legitimately carry zero entries while still serving as the head of a chain with non-empty subsequent pages. `dirIsEmpty` accounts for this: it returns `true` only if both the root page's `sfIndexes` is empty *and* its `sfIndexNext` field is zero. An absent root page is also treated as empty.

## `describeOwnerDir`

`describeOwnerDir` returns a `std::function<void(SLE::ref)>` that stamps `sfOwner = account` onto a newly created directory page. This callback is the conventional third argument to `ApplyView::dirInsert` throughout the codebase — from `RippleStateHelpers.cpp` to `PaymentChannelCreate.cpp` — and is invoked only when `dirInsert` creates a fresh directory node. Returning a function object rather than taking a direct `AccountID` argument allows the insertion logic to defer the initialisation step until it is certain a new page will be created.