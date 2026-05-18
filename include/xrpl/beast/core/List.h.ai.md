# `beast/core/List.h` — Intrusive Doubly Linked List

This file implements `beast::List<T, Tag>`, an intrusive doubly-linked list container living in the `beast` namespace within XRPL's embedded utility library. It solves a specific systems-programming problem: maintaining objects in linked lists without any heap allocation for list bookkeeping, and without the copies that `std::list` would impose.

## Why Intrusive?

A standard `std::list<T>` allocates a separate node on the heap for each element. In hot paths like XRPL's resource management system — which tracks thousands of peer connections across `inbound_`, `outbound_`, `admin_`, and `inactive_` lists simultaneously — those allocations and the pointer indirection they introduce matter. With an intrusive list, the linkage lives directly inside the object. Moving a `Resource::Entry` from the inbound list to the inactive list is a handful of pointer swaps with no allocator involvement.

This design also means the list never owns its elements. When `erase()` removes a node, the object continues to exist unmodified; only its `m_next`/`m_prev` pointers are left dangling. The caller holds responsibility for the object's lifetime, which suits the XRPL pattern of storing entries in a hash map (`Table`) and maintaining intrusive lists as secondary views into that same population.

## Structure

Three components work together:

**`detail::ListNode<T, Tag>`** is the intrusive portion. An element type becomes list-eligible by publicly inheriting from `List<T, Tag>::Node` (which resolves to this class). It holds only two raw pointers, `m_next` and `m_prev`, both defaulted to `nullptr`. Both `List` and `ListIterator` are declared `friend` to access these fields directly. The `Tag` template parameter is what allows an object to inhabit multiple simultaneous lists: each distinct tag produces a distinct `ListNode` base class, so the object carries independent `m_next`/`m_prev` pairs for each list type.

**`detail::ListIterator<N>`** is a bidirectional iterator parameterised on the node type `N`. The const/non-const duality is handled elegantly through the `CopyConst<T, U>` trait: when `N` is a `const`-qualified node type, `value_type` inherits that constness, yielding a `const_iterator` without any code duplication. Dereferencing uses `static_cast<reference>(*m_node)`, which is safe because the node type is always a base class of the element type.

**`List<T, Tag>`** is the container. It holds two sentinel nodes — `m_head` and `m_tail` — rather than raw pointers. `m_head.m_prev` is set to `nullptr` on construction to mark it as the head sentinel, and `m_tail.m_next` is `nullptr` to mark the tail. This sentinel design means `begin()` returns an iterator to `m_head.m_next` and `end()` returns an iterator to `&m_tail`, so all insert and erase operations work uniformly at every position including the boundaries, with no special-case null checks. The list is non-copyable and non-assignable, preventing accidental shallow copies that would break the intrusive invariants.

## The Tag Pattern in Practice

`Resource::Logic` illustrates the expected usage. `Entry` derives from `beast::List<Entry>::Node`, and `Logic` maintains four `EntryIntrusiveList` objects: `inbound_`, `outbound_`, `admin_`, and `inactive_`. The code comments explicitly note that because these are intrusive lists an `Entry` can be in at most one of them at any instant, and must be removed from the current list before being placed in another. This is the one-list-at-a-time constraint that the `Tag` mechanism exists to relax: if `Entry` needed to appear in two lists concurrently, it would inherit from two differently-tagged `List<Entry, TagA>::Node` and `List<Entry, TagB>::Node` bases.

## Bulk Operations and `swap`

The list-to-list `insert(iterator, List&)` overload splices an entire list into another in O(1) time, then clears the source. `prepend()`, `append()`, and `swap()` all build on this. The `swap()` implementation routes through a temporary list using `append()` calls — it does not use pointer tricks — which is correct but means `swap` does three list splices. This is acceptable since swap on intrusive containers is uncommon.

## Absent Safeguards

There is no mechanism to detect double-insertion (inserting a node that already belongs to a list), which would corrupt both lists. The invariants documented in each method — "the element must not already be in the list", "the element must exist in the list" — are stated in comments but are not enforced at runtime. Similarly, `clear()` deliberately does not free elements and does not zero the node pointers of removed elements; calling `clear()` leaves all previously-listed objects with stale `m_next`/`m_prev` pointers until they are re-inserted or destroyed. These are the standard tradeoffs of intrusive container design: maximum performance, minimum safety net.