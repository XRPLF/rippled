# `ValidatorSite.cpp` — Periodic Validator List Fetcher

## Role in the System

XRPL nodes maintain a set of trusted validators — the validators whose signatures they count when forming consensus. That trust set comes from signed *validator lists* published by trusted list-publishers. `ValidatorSite.cpp` implements `ValidatorSite`, the component responsible for discovering those lists: it reads a set of configured URIs, fetches JSON list payloads on a recurring schedule, verifies their structure, and hands them to `ValidatorList::applyListsAndBroadcast()` for cryptographic validation and peer propagation. Without this component, a node would never learn of validator set changes from its configured list-publishers.

## URI Parsing and the Three-Resource Model

Every configured URL becomes a `Site` object. Each `Site` holds three distinct `shared_ptr<Resource>` pointers, and the distinction matters:

- **`loadedResource`**: The original URI as read from configuration. Never changes.
- **`startingResource`**: The URI actually fetched at each scheduled interval. Initially the same as `loadedResource`, but updated to the redirect destination when a *permanent* redirect (301 or 308) is received, so future polls go directly to the canonical address.
- **`activeResource`**: The URI of the in-flight request. Changes during redirect chains but resets at the start of each fresh poll cycle.

This three-way split means `getJson()` can always report what the operator configured *and* where the node is actually fetching from. Permanent redirect updates to `startingResource` are an optimization: after discovering a permanent redirect once, the node skips the intermediate URL on every subsequent poll rather than chasing the redirect every time.

The `Site::Resource` constructor is the sole point of URI validation. It calls `parseUrl()` and then applies scheme-specific rules: `file:` URIs must have no hostname and a non-empty path; `http:` and `https:` URIs must have a hostname; absent ports default to 80 and 443 respectively. On Windows, a leading `/` is stripped from the file path. Any violation throws `std::runtime_error`, causing `load()` to return `false` immediately rather than proceeding with a broken site.

## Async Fetch Lifecycle

The timer is driven by Boost.ASIO's `basic_waitable_timer`. `setTimer()` scans `sites_` for the entry with the earliest `nextRefresh` timestamp and arms the timer to fire at that moment. The timer fires `onTimer()`, which resets the redirect counter, advances `nextRefresh` by the site's `refreshInterval`, and calls `makeRequest()`.

`makeRequest()` dispatches to one of three `detail::Work` subclasses depending on scheme:
- **`WorkSSL`** — TLS-wrapped HTTP, initialized with the application's SSL context and configuration.
- **`WorkPlain`** — Plain HTTP.
- **`WorkFile`** — Reads a local file via the ASIO strand, delivering its contents as a string.

The `Work` interface is intentionally minimal: `run()` and `cancel()`. The caller holds only a `std::weak_ptr<detail::Work>` (`work_`) in `ValidatorSite`, which allows `stop()` to cancel work-in-progress without extending the object's lifetime — the lambda closures inside `makeRequest()` hold the strong reference for as long as the I/O operation is active.

Completion is handled by two different callbacks: `onSiteFetch()` for HTTP/HTTPS responses (which carries a full `boost::beast::http::response`) and `onTextFetch()` for file responses (which carries the raw string). Both ultimately call `parseJsonResponse()` and then re-arm the timer via `setTimer()`.

## Dual Use of the Timer and Request Timeout

A subtle but important design choice: `ValidatorSite` uses the *same* `timer_` object for two distinct purposes. After `makeRequest()` starts the network operation, it immediately overwrites the timer with a fresh `expires_after(requestTimeout_)` deadline, arming `onRequestTimeout()`. The response handler (`onSiteFetch` / `onTextFetch`) begins by calling `timeoutCancel()`, which fires `timer_.cancel_one()` to discard the timeout watchdog before it fires. If the request exceeds the deadline instead, `onRequestTimeout()` calls `work_.cancel()` to abort the in-flight I/O. This works because only one purpose is active at a time, but the tight coupling means both the fetch completion callback and the timeout handler must be careful not to interfere with each other — hence the guard checking `ec != boost::asio::error::operation_aborted` in `onTimer()` and checking `site.activeResource` before logging in `onRequestTimeout()` (the comment there acknowledges the rare race where both can be queued simultaneously).

## Concurrency and Lock Discipline

The class uses two mutexes with a strict acquisition order documented in the header: `sites_mutex_` must always be locked *before* `state_mutex_`. The private helper methods (`setTimer`, `makeRequest`, `parseJsonResponse`, `processRedirect`) accept their required locks by `const& std::lock_guard`, a compile-time proof that callers hold the appropriate lock. This pattern prevents accidental calls from unlocked contexts while avoiding redundant lock acquisitions.

`fetching_`, `pending_`, and `stopping_` are `std::atomic<bool>`, allowing `join()` and `stop()` to wait on a `std::condition_variable` (`cv_`) without spinning. The destructor handles the case where `stop()` may already have been initiated externally by checking `stopping_` before calling `stop()` again, and waits for `fetching_` to clear via the condition variable rather than calling `stop()` a second time.

## JSON Response Parsing and List Application

`parseJsonResponse()` validates the mandatory `manifest` and `version` fields, then delegates blob parsing to `ValidatorList::parseBlobs()` which understands both v1 (single blob) and v2 (multiple blobs) list formats. The function computes `sha512Half(manifest, blobs, version)` as the list's content hash and passes it to `applyListsAndBroadcast()`. That function handles cryptographic verification, deduplication, and peer broadcast. The result carries a map of `ListDisposition` outcomes — accepted, stale, untrusted, same_sequence, etc. — which are logged individually at appropriate severity levels.

The server-controlled `refresh_interval` field in the response allows publishers to tune their polling frequency dynamically. `ValidatorSite` clamps the value to `[1min, 24h]` before applying it, protecting against misconfigured or malicious sites that might attempt to suppress future fetches by sending an extremely long interval or exhaust the scheduler with a zero interval.

## Fallback to Locally Cached Lists

`missingSite()` is called both during initial `load()` when the URI list is empty and inside `onSiteFetch()` when a fetch fails. It calls `ValidatorList::loadLists()` to retrieve any locally persisted copies of previously fetched validator lists, then calls `load()` on those paths. This provides resilience: a node that loses connectivity to all its configured sites can still operate with the most recently fetched list rather than being left with no validators at all.

## Redirect Handling

`processRedirect()` enforces a cap of `max_redirects = 3` per poll cycle and explicitly forbids `file:` scheme redirects — only HTTP and HTTPS are valid redirect destinations. The redirect counter is reset to zero at the start of each fresh `onTimer()` cycle so that a series of valid redirects spread across poll intervals does not permanently exhaust the redirect budget.