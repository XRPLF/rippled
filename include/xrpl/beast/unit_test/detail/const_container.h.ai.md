# `detail/const_container.h` — Read-Only Container Adapter

`const_container` is a small CRTP-style base class template in `beast::unit_test::detail` that solves a narrow but recurring problem in the unit-test framework's result hierarchy: how do you let a class own a standard container internally while exposing only read-only access to callers, yet still let the owning class itself mutate that container?

## Design Intent

The framework builds up a three-level result tree — individual test conditions aggregate into `case_results`, which aggregate into `suite_results`, which aggregate into the top-level `results` class. Each level needs to store a sequence (a `std::vector` or `std::set`) and provide public iteration and size queries, but mutation — inserting new entries, tracking failure counts — must be controlled by the class itself, not by external code.

A naive approach would be to make the container a `private` member and write `begin()`/`end()` accessors that return `const_iterator`. `const_container` formalises exactly that pattern into a reusable base, eliminating boilerplate across the four distinct container-owning types in `results.h` and `suite_list.h`.

## Structure

`const_container<Container>` stores the underlying container as a `private` member `m_cont`. Two `protected` overloads of `cont()` expose it — one mutable, one `const` — so only derived classes can write to it. The public interface is intentionally narrow: `empty()`, `size()`, `begin()`, `cbegin()`, `end()`, and `cend()`. Critically, both `iterator` and `const_iterator` are aliased to `cont_type::const_iterator`, so even when a caller holds a non-const reference to the derived class, the iterators they receive are immutable. There is no `operator[]`, no `front()`/`back()`, and no mutation path whatsoever at the public level.

## How Derived Classes Use It

Derived classes call `cont()` to reach the underlying container. In `results.h`, `suite_results::insert()` calls `cont().emplace_back(...)` and `case_results::tests_t::pass()` calls `cont().emplace_back(true)` — mutating the container through the `protected` accessor while keeping the public interface read-only. In `suite_list.h`, `suite_list::insert<Suite>(...)` calls `cont().emplace(...)` on the underlying `std::set<suite_info>`. The symmetry is clean: the template parameter changes the container type; the access discipline stays constant.

## Why Not Inheritance from the Container Directly?

Publicly inheriting from `std::vector` or `std::set` would expose the entire mutable interface — `push_back`, `insert`, `erase`, `clear` — to all callers. Private inheritance would suppress the mutable members but would also require `using` declarations to re-expose each desired read-only member, which is what `const_container` does implicitly by forwarding calls to `m_cont`. Composition with a `protected` accessor is the cleaner middle ground: derived classes get full write access, callers get none.

## Scope and Placement

The `detail` namespace signals that `const_container` is an implementation aid, not part of the framework's public API. Nothing outside `beast::unit_test` depends on it directly. Its template parameter is unconstrained — any type satisfying the standard container concept (providing `value_type`, `size_type`, `difference_type`, `const_iterator`, `empty()`, `size()`, `cbegin()`, `cend()`) will work, which is why it can be reused with both `std::vector` and `std::set` without modification.