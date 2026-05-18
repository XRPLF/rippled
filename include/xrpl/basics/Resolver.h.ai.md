# `include/xrpl/basics/Resolver.h`

## Role and Purpose

`Resolver.h` defines the `xrpl::Resolver` abstract interface — the single extension point through which the XRPL node resolves DNS hostnames at runtime. Its job is narrow: given a batch of `host:port` strings, asynchronously resolve each one into a list of `beast::IP::Endpoint` values and deliver the results to a caller-supplied callback. The interface abstracts away the underlying I/O mechanism so that production code uses a real Boost.Asio resolver while tests can substitute a mock without touching callers.

## Interface Design

The class is intentionally minimal. It declares four pure-virtual methods: `start()`, `stop()`, `stop_async()`, and `resolve()`. These map directly to the lifecycle and operation of a background I/O service. The destructor is pure virtual with a non-inline out-of-line definition (provided in `ResolverAsio.cpp`), which is the standard C++ idiom for giving an abstract base class a vtable anchor without a concrete body in the header.

The `HandlerType` alias, `std::function<void(std::string, std::vector<beast::IP::Endpoint>)>`, captures the callback contract. Each resolved name triggers one invocation, receiving both the original hostname string and the resulting address list. This pairing matters: callers often need to correlate results back to the name they submitted (e.g., to log which peer seed produced which IP), and re-passing the name through the completion avoids callers having to maintain their own lookup tables.

## The Template/Virtual Overload Pair

The `resolve()` method appears twice: once as a `virtual` function taking `HandlerType const&`, and once as a non-virtual `template<class Handler>` that wraps any callable into `HandlerType` before forwarding to the virtual overload. This is the non-virtual interface (NVI) pattern applied to templates. The motivation is that if the template version were virtual, every instantiation would need a vtable entry — impractical for a polymorphic class. Instead, the template normalises the input type, and the virtual function carries the actual polymorphic dispatch. Callers get the convenience of passing lambdas directly without boilerplate `std::function` construction.

## Lifecycle Contract

The three lifecycle methods reflect a service that runs on a shared Boost.Asio `io_context`. `start()` registers the resolver's reference in the pending I/O counter; `stop_async()` posts a cancellation request to the resolver's strand and returns immediately; `stop()` combines `stop_async()` with a blocking wait on a condition variable until all in-flight handlers drain. This two-phase shutdown pattern lets callers choose between fire-and-forget teardown (during orderly application shutdown where the io_context will be drained anyway) and synchronous teardown (when you need a hard guarantee that the resolver is idle before proceeding).

## Concrete Implementation: `ResolverAsioImpl`

The only production implementation is `ResolverAsioImpl`, which lives entirely inside `ResolverAsio.cpp` and is exposed only through the `ResolverAsio::New()` factory. This internal linkage is deliberate: the implementation is never constructed directly; ownership flows through `std::unique_ptr<ResolverAsio>`.

`ResolverAsioImpl` inherits from both `ResolverAsio` (which extends `Resolver`) and `AsyncObject<ResolverAsioImpl>`, a CRTP mixin that reference-counts outstanding completion handlers via an atomic integer. When the count drops to zero, `asyncHandlersComplete()` fires and notifies the condition variable that `stop()` is waiting on. The `CompletionCounter` RAII type is bound into every async handler so the count is maintained correctly even under cancellation paths.

Work items are queued as `Work` structs in a `std::deque<Work>`. A subtle optimisation: names within a `Work` item are stored in reverse order (via `std::reverse_copy`), so `do_work()` pops from the back of the vector in O(1) rather than the front. The strand ensures that the queue is only ever accessed from the `io_context` thread, making no additional locking necessary on the deque itself.

The `parseName()` helper handles two cases: if the string parses as a fully-qualified `beast::IP::Endpoint` (a raw IP address with port), it extracts the components directly without a DNS lookup. Otherwise it falls back to splitting on whitespace and `:` delimiters. This means callers can freely mix hostnames, plain IP addresses, and `host port` strings in the same batch.

## Usage in Context

`Application` holds a `std::unique_ptr<ResolverAsio>` created at startup and passed to `OverlayImpl`. The overlay calls `resolve()` twice during startup: once for the hardcoded bootstrap IPs (including well-known XRPL Commons hub addresses) and once for the `[ips_fixed]` entries from the node's config file. Both calls use lambdas that convert the resulting `beast::IP::Endpoint` addresses into the PeerFinder's known-peers list. No caller ever interacts with the concrete `ResolverAsioImpl` type — all access flows through the `Resolver` interface, keeping the overlay's dependency on DNS mechanics entirely behind the abstraction.