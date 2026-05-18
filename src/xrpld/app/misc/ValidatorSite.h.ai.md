# `ValidatorSite.h` — Remote Validator List Fetcher

## Role and Purpose

`ValidatorSite` is the component responsible for keeping a rippled node's local view of trusted validators current by periodically fetching signed validator lists from remote URLs. Validator lists are the mechanism XRPL uses to establish which nodes a given server considers trustworthy consensus participants. Because these lists are signed by well-known publishers and carry an expiry timestamp, they must be refreshed before they expire — that is the core problem this class solves.

The class sits between the network I/O layer (the `Work` hierarchy) and the cryptographic trust layer (`ValidatorList`). It handles the full lifecycle: URI parsing, HTTP/HTTPS/file transport selection, redirect following, JSON parsing, and finally handing off a verified payload to `ValidatorList::applyListsAndBroadcast()`.

## The `Site` Struct and Three-Pointer Resource Model

The most subtle data structure in the header is the nested `Site`, which tracks three distinct `shared_ptr<Resource>` objects for a single configured URL:

- `loadedResource` — the URI exactly as it appeared in the config file; never changes after construction.
- `startingResource` — the URI used at the start of each scheduled refresh cycle; equals `loadedResource` initially, but is updated when a *permanent* HTTP redirect (301/308) is received. This means the server automatically tracks permanent moves without requiring operator intervention.
- `activeResource` — the URI currently being requested within a single cycle; equals `startingResource` except when a temporary redirect (302/307) is in flight. It is cleared to `nullptr` once the fetch completes, which serves as the guard condition inside `onRequestTimeout` to detect the rare race where both the timeout and the response handler are simultaneously queued.

This three-pointer design carefully encodes redirect semantics: permanent redirects update the baseline for future cycles; temporary redirects are followed inline but forgotten afterward. Redirects are capped at `max_redirects = 3` to prevent loops.

## Concurrency Model

The class uses two mutexes with a rigidly enforced acquisition order, documented explicitly in the header:

> *If both mutex are to be locked at the same time, `sites_mutex_` must be locked before `state_mutex_` or we may deadlock.*

`sites_mutex_` protects the `sites_` vector and all per-site state. `state_mutex_` protects timer scheduling, `fetching_`, `pending_`, and `stopping_` state. The three atomic flags (`fetching_`, `pending_`, `stopping_`) allow lightweight checks without taking either lock in performance-sensitive paths.

Private methods that require a lock already held accept `std::lock_guard<std::mutex> const&` by reference as a proof-of-holding parameter. This is a compile-time enforcement idiom: the caller is forced to name the guard, making the locking intent explicit and preventing accidental calls on unlocked state. `setTimer()`, `makeRequest()`, `parseJsonResponse()`, `processRedirect()`, and `missingSite()` all use this pattern.

A `std::condition_variable cv_` coordinates the lifecycle methods: `join()` waits for `!pending_`, `stop()` waits for `!fetching_`, and the destructor participates in this same wait if `stop()` has already been called concurrently.

## Timer-Driven Scheduling Loop

`setTimer()` scans `sites_` for the entry with the earliest `nextRefresh` time and arms a single `boost::asio::basic_waitable_timer` against that deadline. When the timer fires, `onTimer()` records a new `nextRefresh` (current time plus `refreshInterval`), resets the redirect counter, and calls `makeRequest()` with `startingResource`. After each completed fetch — whether successful or failed — `onSiteFetch()` or `onTextFetch()` calls `setTimer()` again, perpetuating the cycle.

The default refresh interval is 5 minutes, but a remote site can override it via the optional `refreshInterval` JSON field (clamped to [1, 1440] minutes). Fetch errors use a faster `error_retry_interval` of 30 seconds so transient outages recover promptly.

The overall design processes only one site at a time: a single `std::weak_ptr<detail::Work> work_` and the `fetching_` flag ensure no two fetch operations overlap, simplifying state management at the cost of some parallelism.

## Transport Abstraction via `Work`

`makeRequest()` selects a concrete `Work` subclass based on the URI scheme:

- `WorkSSL` for `https://` — uses Boost.Asio with SSL context from the application config, with a remembered endpoint (`lastRequestEndpoint`) and a connection-reuse hint (`lastRequestSuccessful`) to skip redundant DNS lookups on repeated hits to the same host.
- `WorkPlain` for `http://` — same endpoint caching, no TLS.
- `WorkFile` for `file://` — reads up to 1 MB from a local path asynchronously via `boost::asio::strand`, useful for testing or air-gapped deployments with pre-distributed list files.

A request timeout of 20 seconds (configurable at construction, primarily for tests) is implemented by setting the same `timer_` to fire after `requestTimeout_` immediately after `Work::run()` is called. The `timeoutCancel` lambda captures the reverse: when the response arrives first, it calls `timer_.cancel_one()` to prevent a spurious timeout from running.

## JSON Parsing and List Application

`parseJsonResponse()` validates the JSON envelope — `manifest`, `version`, `blob`(s), and `signature` — then calls `ValidatorList::parseBlobs()` for version-specific payload extraction, and hands the result to `ValidatorList::applyListsAndBroadcast()`. This method both updates the local validator trust state and propagates the list to connected peers via the overlay network. The response is hashed with `sha512Half` over manifest, blobs, and version before broadcast, so peers can deduplicate via `HashRouter`.

`ListDisposition` return values from `applyListsAndBroadcast` are logged per-disposition at debug or warn level and stored in `Site::Status::disposition` for the `getJson()` status API. The disposition ordering is itself semantically significant: values are defined from "best" to "worst" so `bestDisposition()` can return the most informative result when multiple blobs are applied in a single response.

## Fallback to Local Cache

`missingSite()` is called in two situations: when no site URIs are configured at all, and when a fetch fails. It asks `ValidatorList::loadLists()` for any previously cached list files and attempts to load them as if they were fetched from remote. This prevents a validator from losing its trusted-validator configuration purely due to transient network unavailability, falling back gracefully to the last known good state.