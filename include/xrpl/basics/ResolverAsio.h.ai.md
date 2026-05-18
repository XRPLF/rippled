# `include/xrpl/basics/ResolverAsio.h`

## Role in the System

`ResolverAsio.h` is the public header for the Boost.Asio-backed implementation of the abstract `Resolver` interface. Its purpose is narrow: it introduces one concrete subclass, `ResolverAsio`, and exposes a single static factory method. The header's brevity is intentional — it acts as an opaque handle into a non-trivial implementation that the caller never sees directly.

## The Abstraction Layer

`ResolverAsio` extends `Resolver`, the abstract base class defined in `Resolver.h`. That base declares the entire observable contract: `start()`, `stop()`, `stop_async()`, and a templated `resolve()` that accepts a list of hostname strings and a completion handler of type `std::function<void(std::string, std::vector<beast::IP::Endpoint>)>`. By defining `ResolverAsio` as a pure intermediate class (its constructor is `= default` and defaulted) with only a static `New()` factory, the header ensures that client code depends solely on the `Resolver` interface. The concrete work happens exclusively inside `ResolverAsioImpl`, which is defined entirely within `ResolverAsio.cpp` and is therefore invisible to any translation unit that includes this header.

This two-level separation — abstract `Resolver` base, thin `ResolverAsio` header, hidden `ResolverAsioImpl` body — is a classic pImpl-adjacent pattern. The difference from a true pImpl is that the indirection goes through virtual dispatch rather than a pointer-to-impl member. The effect is the same: implementation details, including the Asio strand, the internal work queue, and the `AsyncObject` mixin, are completely insulated from the public API surface.

## The Factory and Ownership Model

`ResolverAsio::New(boost::asio::io_context&, beast::Journal)` returns a `std::unique_ptr<ResolverAsio>`. The calling site in `Application.cpp` stores the result as a member and later accesses it through the `Resolver*` interface. Ownership is unambiguous: whoever holds the `unique_ptr` owns the object and is responsible for calling `stop()` before destruction. The destructor assertions in `ResolverAsioImpl` enforce this — destroying the object with pending I/O or without stopping first triggers `XRPL_ASSERT` failures.

The `io_context` reference is non-owning and must outlive the resolver. This is a well-understood contract in Asio programming: the context drives all I/O and must not be destroyed before the objects that post work to it.

## Why a Static Factory Over a Public Constructor?

Making the constructor `explicit ... = default` while routing all real construction through `New()` ensures that the concrete `ResolverAsioImpl` type — with all its Asio internals — never needs to be named by consumers. This also gives the factory freedom to perform any pre-construction initialization and return the object as the abstract base pointer, guaranteeing that the caller can only interact through the `Resolver` interface without a cast.

## Relationship to `beast::Journal`

The `beast::Journal` parameter to `New()` is threaded directly into the implementation for structured logging of resolution queuing, stop events, and parse failures. It is stored by value rather than by reference, which is the standard practice for `Journal` — it is a lightweight handle that is cheap to copy.