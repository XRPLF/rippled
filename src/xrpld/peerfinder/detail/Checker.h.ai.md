# `Checker.h` — Async Reachability Probe for PeerFinder

## Purpose

`Checker` sits inside the `PeerFinder` subsystem and answers one question: *can the rest of the network actually reach a peer at its advertised listening address?* When an inbound peer connects and reports the address it claims to accept incoming connections on, XRPL cannot simply trust that address — the port might be firewalled, NAT-translated incorrectly, or entirely wrong. `Checker` performs an outbound TCP connection attempt to that reported endpoint, then reports success or failure back to `Logic`. The result is recorded on the `SlotImp` as `canAccept`, gating whether that address is ever propagated to other peers via the live cache.

## Class Structure

`Checker<Protocol>` is templated on a Boost.Asio protocol type (defaulting to `boost::asio::ip::tcp`), primarily to enable substitution of a mock protocol in tests. It manages a collection of in-flight async operations using a `boost::intrusive::list` of `basic_async_op` objects, protected by a `std::mutex` and a `std::condition_variable`.

The internal type hierarchy achieves type erasure in two layers. `basic_async_op` is a polymorphic base with virtual `stop()` and `operator()(error_code)` methods, and it embeds a `boost::intrusive::list_base_hook` directly — eliminating the extra heap allocation that `std::list<std::unique_ptr<...>>` would require. The concrete `async_op<Handler>` template inherits from it, binding together a `socket_type`, the caller-supplied completion handler, and a back-reference to the owning `Checker`.

## Ownership and Lifetime Contract

The most subtle aspect of the design is how `async_op` lifetime is managed. In `async_connect`, the op is created as a `std::shared_ptr<async_op<Handler>>`, pushed into the intrusive list, and then the socket's `async_connect` is called with a lambda that captures that same `shared_ptr`:

```cpp
op->socket_.async_connect(
    ...,
    std::bind(&basic_async_op::operator(), op, std::placeholders::_1));
```

The lambda keeps the `async_op` alive for the entire duration of the asynchronous operation — regardless of what the caller does after returning from `async_connect`. When the I/O completes (or is canceled), Asio invokes the lambda, which dispatches through the virtual `operator()` to the user's handler. When the lambda is destroyed, the `shared_ptr` reference count drops to zero, the `async_op` destructor runs, and it calls `checker_.remove(*this)`. `remove()` erases the op from the intrusive list under lock and signals the condition variable if the list is now empty.

This pattern is idiomatic "self-managing async operation" design: the op registers itself into a tracked collection at construction and deregisters at destruction, with no separate cleanup step required.

## Stop and Wait Protocol

`stop()` marks `stop_ = true` under lock, then calls `socket_.cancel(ec)` on every live operation. This issues asynchronous cancellation — it does not wait for completion and returns immediately. Handlers will receive `operation_aborted` errors rather than completion values.

`wait()` blocks on `cond_` until the intrusive list is empty, meaning all pending handlers (whether they completed normally or with `operation_aborted`) have run and their `async_op` destructors have executed.

The destructor calls only `wait()`, not `stop()`. This is intentional: destruction just drains whatever is pending; if you want cancellation before destruction, you must call `stop()` first. In `ManagerImp`, this sequencing is explicit:

```cpp
void stop() override {
    work_.reset();
    checker_.stop();   // cancel pending I/O
    m_logic.stop();
}

~ManagerImp() override { stop(); }  // Checker::~Checker() then calls wait()
```

## Integration with Logic

`Logic<Checker>` holds a reference to `Checker` as a template parameter, keeping the dependency injectable and mockable. When a newly-connected inbound peer advertises its listening endpoints via `on_endpoints`, `Logic` calls:

```cpp
m_checker.async_connect(
    ep.address,
    std::bind(&Logic::checkComplete, this,
              slot->remote_endpoint(), ep.address,
              std::placeholders::_1));
```

The `checkComplete` callback receives the `error_code`. If it is `operation_aborted`, it returns silently (the slot was already torn down). Otherwise, a non-zero error sets `slot.canAccept = false` and notifies the boot cache of a failure; success sets `canAccept = true` and records the confirmed listening port on the slot. Only after this check passes is the peer's address eligible to enter the live cache and be gossiped to other nodes.

The `async_connect` documentation explicitly notes that "the execution guarantees offered by asio handlers are NOT enforced," meaning the handler runs on whatever thread services the `io_context`. `Logic::checkComplete` accordingly takes `lock_` immediately upon entry to safely access the `slots_` map.