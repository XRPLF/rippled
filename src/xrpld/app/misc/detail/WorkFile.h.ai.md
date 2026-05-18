# WorkFile.h — Local-File Variant of the Work Abstraction

## Purpose and Context

`WorkFile` is one of three concrete implementations of the `Work` interface in the validator-list fetching subsystem. The other two — `WorkPlain` and `WorkSSL` (both derived from the CRTP template `WorkBase`) — handle HTTP and HTTPS validator list sources respectively. `WorkFile` covers the third URL scheme: `file://`, allowing a node operator to point the validator-list configuration at a path on the local filesystem instead of a remote server.

In `ValidatorSite.cpp`, the three classes are used interchangeably: after parsing the configured URL, `ValidatorSite` constructs either a `WorkPlain`, `WorkSSL`, or `WorkFile` instance depending on the scheme, stores the result in a `std::shared_ptr<Work>`, and calls `run()`. The rest of the fetch lifecycle — cancellation on timeout, retry scheduling — operates uniformly through the base `Work` interface.

## Class Design

`WorkFile` inherits directly from `Work` (not from `WorkBase`) because the HTTP machinery in `WorkBase` — DNS resolution, TCP connection, Beast HTTP read/write, endpoint tracking — is entirely irrelevant for a local file read. This avoids dragging in socket state and resolver logic for a straightforward filesystem operation.

One subtle consequence is that `WorkFile` **overrides the `response_type`** alias. At namespace scope in `Work.h`, `response_type` is defined as `boost::beast::http::response<boost::beast::http::string_body>`. `WorkFile` redefines it locally as `std::string`. This lets the callback type be:

```cpp
using callback_type = std::function<void(error_code const&, response_type const&)>;
```

… where `response_type` resolves to `std::string` within `WorkFile`'s scope. The HTTP-based `WorkBase::callback_type` also includes a `endpoint_type` argument (because `ValidatorSite` records the endpoint for retry prioritization), but `WorkFile`'s callback omits that: there is no network endpoint to record.

`WorkFile` uses `std::enable_shared_from_this<WorkFile>` so that `run()` can safely extend its own lifetime when posting to the strand, a pattern required by Asio's asynchronous dispatch model.

## Execution Model

`run()` uses a strand to serialize execution:

```cpp
if (!strand_.running_in_this_thread())
    return boost::asio::post(
        ios_,
        boost::asio::bind_executor(strand_, std::bind(&WorkFile::run, shared_from_this())));
```

If `run()` is called from outside the strand (the common case — `ValidatorSite` calls it from its own strand), it re-posts itself to `ios_` bound to the `WorkFile` strand. Once on the strand, it calls `getFileContents(ec, path_, megabytes(1))` synchronously, then fires the callback and nulls it out.

The 1 MB cap passed to `getFileContents` is a deliberate resource guard. A malformed or hostile validator-list file cannot cause unbounded memory allocation; `getFileContents` will return an error if the file exceeds this limit.

## Lifecycle and Safety Invariants

The destructor encodes a critical safety invariant shared with `WorkBase`:

```cpp
WorkFile::~WorkFile()
{
    if (cb_)
        cb_(make_error_code(boost::system::errc::interrupted), {});
}
```

If the object is destroyed before `run()` has fired the callback (e.g., because the work was cancelled before it started, or the `io_context` was torn down), the destructor ensures the caller's callback is always invoked exactly once — never silently dropped. `WorkBase` applies the same pattern using `errc::not_a_socket` instead of `errc::interrupted`, each chosen to signal the appropriate failure mode to the caller.

After `run()` fires the callback it immediately sets `cb_ = nullptr`, which prevents the destructor from invoking it a second time. This single-invocation guarantee is enforced by the `XRPL_ASSERT(cb_, ...)` placed just before the call in `run()`, catching programmer errors where the callback has already been consumed.

## `cancel()` Is a No-op

The `cancel()` override is intentionally empty. Unlike `WorkBase::cancel()`, which must interrupt an in-flight async TCP/DNS operation, `WorkFile` has no cancellable I/O: the file read is synchronous within the posted task. Once the task has been posted, either it will run and complete, or the `io_context` will shut down and the destructor's safety callback will fire. There is no intermediate state that requires intervention.