# `LockFreeStack.h` — Intrusive Lock-Free MPMC Stack

## Role in the System

`LockFreeStack` is a foundational concurrency primitive in the `beast` namespace, providing a Multiple-Producer Multiple-Consumer (MPMC) intrusive stack implemented with atomic compare-and-swap (CAS) operations. It lives alongside `List.h` — the intrusive doubly-linked list — and shares the same intrusive node-inheritance idiom. The primary consumer in the XRPL codebase is `Workers`, the thread pool implementation, which uses two simultaneous `LockFreeStack` instances to track all created workers and the subset that are currently paused.

## Intrusive Design and the `Tag` Mechanism

Like `beast::List`, `LockFreeStack` is intrusive: rather than allocating a wrapper node internally, it requires the element type to embed the `Node` type (typically by public inheritance). This eliminates heap allocations from `push_front`/`pop_front` entirely, a meaningful gain in a hot-path thread pool.

The `Tag` template parameter is the key to using a single object in multiple stacks simultaneously. When `Workers` manages its thread pool:

```cpp
class Worker : public beast::LockFreeStack<Worker>::Node,
               public beast::LockFreeStack<Worker, PausedTag>::Node
```

The two `Node` base classes carry separate `m_next` pointers — one for the `m_everyone` stack (all workers ever created) and one for `m_paused` (only paused workers). Without tags, inheriting `Node` twice would be ambiguous and collapse the two link fields into one. The same idiom is documented extensively in `List.h` for doubly-linked lists.

## Internal Representation: The Sentinel End Node

The stack stores `m_end` as a `Node` member by value and uses it as the terminal sentinel — `m_head` points to `&m_end` when the stack is empty. This avoids a null pointer as the terminator, which matters for two reasons: it keeps the iterator's end comparison simple (`iterator(&m_end)` rather than `iterator(nullptr)`), and it gives `empty()` a single `m_head.load() == &m_end` check without a potential dereference.

## Lock-Free Push and Pop

Both `push_front` and `pop_front` implement the standard CAS retry loop:

```cpp
while (!m_head.compare_exchange_strong(
    old_head, node,
    std::memory_order_release, std::memory_order_relaxed))
```

The release ordering on success ensures that any writes to the node before it is pushed are visible to threads that subsequently load the head. The relaxed ordering on failure only updates the local `old_head` snapshot, so no ordering guarantee is wasted on failed attempts.

`push_front` returns `true` if and only if the stack was empty before the push. This is a deliberate design: it allows the caller to detect the "first item added" event without a separate `empty()` check that would create a time-of-check/time-of-use (TOCTOU) race in a concurrent context. `Workers::addTask()` can use this signal to decide whether to wake a thread without an additional atomic read.

`pop_front` returns `nullptr` when the stack is empty, giving callers an idiomatic ownership-transfer interface: the returned raw pointer represents the transferred ownership of the node back to the caller, and `nullptr` signals the empty case without an exception.

There is a `VFALCO NOTE` comment on `push_front` observing that it takes a `Node*` (pointer) while the intrusive `List` interface takes a reference. This is a minor style inconsistency that was never resolved.

## The ABA Problem — Explicit Caller Responsibility

The header prominently documents that the implementation does not protect against the ABA problem. In a CAS-based stack, the ABA scenario arises when a thread reads the head pointer (value A), is preempted, another thread pops A, pushes a different node, then re-pushes A to the head — so the CAS in the original thread succeeds spuriously. The result is a corrupted stack.

`LockFreeStack` documents this and explicitly assigns the responsibility for prevention to the caller. In the `Workers` use case, this is safe because workers are long-lived objects (allocated once in the constructor, destroyed only in the destructor) and are never destroyed while the stack is being mutated from other threads.

## Iteration

`LockFreeStackIterator` is a forward-only iterator typed on `<Container, IsConst>`. The `IsConst` boolean drives all pointer and reference type selection through `std::conditional`, giving separate `iterator` and `const_iterator` types from a single implementation. The conversion constructor from `LockFreeStackIterator<Container, OtherIsConst>` is `explicit` to prevent accidental const-stripping, while allowing const-to-const or mutable-to-const conversions.

Crucially, the stack documents that iteration is **not** safe when `push_front` or `pop_front` is called concurrently. The `begin()` call loads the current head atomically, but subsequent `operator++` calls load `m_next` without any protocol to prevent the node from being popped and re-used mid-traversal. The iterator facility is provided for single-threaded traversal scenarios, not as a concurrent view.

## Summary

`LockFreeStack` is a narrow, focused primitive: it does one thing (LIFO queue with atomic push/pop) and defers the harder problems (ABA, memory reclamation, concurrent iteration) to its callers. The intrusive, tag-parameterized design keeps it zero-overhead for the `Workers` use case where multiple simultaneous list memberships are required, and the sentinel node and push return value are small but deliberate choices that remove unnecessary atomic operations from the callers.