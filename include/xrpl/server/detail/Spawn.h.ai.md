# `include/xrpl/server/detail/Spawn.h`

This header exists to paper over a breaking behavioral change introduced in Boost.Asio 1.84, where `boost::asio::spawn` stopped propagating unhandled exceptions from coroutine bodies to the `io_context::run()` call site. The file lives in the `xrpl::util` namespace and is used throughout the server detail layer — `Door.h`, `BaseHTTPPeer.h`, `PlainHTTPPeer.h`, and `SSLHTTPPeer.h` — wherever a coroutine is launched.

## The Problem Being Solved

Before Boost 1.84, an unhandled exception inside a stackful coroutine would unwind through `io_context::run()`, giving callers a clear failure signal. Starting with 1.84, `boost::asio::spawn` accepts an optional completion token (the third argument); when omitted, exceptions are silently swallowed. The XRPL server has no per-connection exception handler, so silent swallowing would leave connections in broken states with no diagnostic trace. The fix is to always supply a completion handler that logs and re-throws.

## `kPROPAGATE_EXCEPTIONS`

The `impl::kPROPAGATE_EXCEPTIONS` inline constexpr lambda is that completion handler. It accepts the `std::exception_ptr` that Asio provides after the coroutine exits. If the pointer is non-null the exception is re-thrown inside a `try/catch` pair: `std::exception` derivatives are logged with `JLOG` before re-throwing; unknown types get an "Unknown" warning. In both paths the exception propagates back to `io_context::run()`, preserving the pre-1.84 contract. The `kPROPAGATE_EXCEPTIONS` object lives in `namespace impl` to signal it is an implementation detail, not a public interface.

## `IsStrand` Concept

The C++20 concept `impl::IsStrand` checks whether a decayed type is exactly `boost::asio::strand<inner_executor_type>`. This single-check concept enables compile-time dispatch inside `spawn()` without a runtime branch. Wrapping an already-stranded executor in a second strand is harmless but wasteful and can produce subtle ordering issues if code elsewhere tests `strand_.running_in_this_thread()` — a check that only the outer strand would pass. The concept guards against that.

## `xrpl::util::spawn()`

The public `spawn()` function is a thin template:

```cpp
template <typename Ctx, typename F>
    requires std::is_invocable_r_v<void, F, boost::asio::yield_context>
void spawn(Ctx&& ctx, F&& func);
```

The `requires` clause enforces that the callable returns `void` when given a `yield_context`, catching mismatches at the earliest possible point. Inside the body, `if constexpr (impl::IsStrand<Ctx>)` branches:

- **Strand path**: forwards `ctx` directly to `boost::asio::spawn`. The executor is already serialized; no additional wrapping needed.
- **Non-strand path**: calls `boost::asio::make_strand(boost::asio::get_associated_executor(ctx))` to create a fresh strand from whatever executor is associated with the context, then passes that strand to `spawn`. This restores the implicit-strand guarantee that older Boost versions provided by default.

In both paths `impl::kPROPAGATE_EXCEPTIONS` is the third argument, ensuring exceptions are never silently dropped.

## Usage Pattern

Every coroutine entry point in the server detail layer — accepting connections in `Door`, reading HTTP requests in `BaseHTTPPeer::do_read`, writing streaming responses via `do_writer`, closing TLS/plain streams in `SSLHTTPPeer` and `PlainHTTPPeer` — is launched through `util::spawn(strand_, ...)`. The callers always already hold an explicit `boost::asio::strand` member, so they hit the fast strand path. The non-strand path exists for future callers that may only have an `io_context` or generic executor at hand.