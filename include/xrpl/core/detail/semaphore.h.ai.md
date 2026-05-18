# `include/xrpl/core/detail/semaphore.h`

## Purpose

This file provides `xrpl::basic_semaphore`, a counting semaphore built on a mutex and condition variable. It exists exclusively as a compiler-bug workaround: both GCC and Clang shipped broken implementations of `std::counting_semaphore` (GCC PR 104928 and LLVM PR 79265), and rippled's supported compiler range includes affected versions — GCC up through 14.x and Clang before 19.1. The file carries an explicit `TODO` to delete it and migrate to `std::counting_semaphore` once the minimum compiler floor advances past GCC 16 or Clang 19.1.

## Design

`basic_semaphore` is a class template parameterized on `Mutex` and `CondVar`, allowing the synchronization primitives to be injected for testing. The `semaphore` type alias fixes those to `std::mutex` and `std::condition_variable`, which is what all production code uses.

The internal state is just three members: a mutex, a condition variable, and a `std::size_t` counter. All three operations follow the standard monitor pattern:

- `notify()` locks the mutex, increments the count, then calls `notify_one()` so exactly one blocked `wait()` caller is woken.
- `wait()` acquires a `std::unique_lock`, then loops on `m_cond.wait(lock)` while the count is zero. The `while` loop (rather than a plain `if`) guards against spurious wakeups, which condition variables are permitted to deliver. Once the count is positive the method decrements it and returns.
- `try_wait()` takes a `std::lock_guard`, checks the count, and either decrements and returns `true` or returns `false` immediately without blocking.

## Role in the Thread Pool

The only consumer in this codebase is `Workers`, the thread-pool implementation in `include/xrpl/core/detail/Workers.h`. `Workers` holds a `semaphore m_semaphore` that acts as the work queue's depth counter: every call to `Workers::addTask()` calls `m_semaphore.notify()`, and every worker thread blocks on `m_semaphore.wait()` when idle. The same `notify()` path is also used to deliver "pause" tokens — signaling a worker that it should suspend itself rather than process a real task. This single semaphore therefore mediates both task dispatch and thread lifecycle management, making its correctness critical to the stability of the entire job-processing system.

## Why Not `std::binary_semaphore` or a Raw `condition_variable`?

A counting semaphore lets the producer race ahead of consumers: if `addTask()` is called three times before any worker wakes, the count becomes three and three workers will each claim one task without any signals being lost. A raw condition variable without the count would silently drop signals if no thread was waiting at notification time, requiring an additional queue or flag. The counting semantic is exactly what the thread pool needs to avoid missed wakeups under bursty load.

## Transient Nature

The header lives in `include/xrpl/core/detail/`, the `detail` subdirectory signalling that it is an implementation-internal facility not intended for external consumers. It is a deliberate stop-gap, not a permanent abstraction. Once the compiler floor is raised the file should be removed, all inclusion sites updated to `<semaphore>`, and occurrences of `xrpl::semaphore` replaced with `std::counting_semaphore<...>`.