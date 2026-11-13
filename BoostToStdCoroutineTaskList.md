# Boost.Coroutine to C++20 Migration — Task List

> Parent document: [BoostToStdCoroutineSwitchPlan.md](BoostToStdCoroutineSwitchPlan.md)

---

## Milestone 1: New Coroutine Primitives

- [ ] **1.1** Design `CoroTask<T>` class with `promise_type`
  - Define `promise_type` with `initial_suspend`, `final_suspend`, `unhandled_exception`, `return_value`/`return_void`
  - Implement `FinalAwaiter` for continuation support
  - Implement move-only RAII handle wrapper
  - Support both `CoroTask<T>` and `CoroTask<void>`

- [ ] **1.2** Design and implement `JobQueueAwaiter`
  - `await_suspend()` calls `jq_.addJob(type, name, [h]{ h.resume(); })`
  - Handle `addJob()` failure (shutdown) — resume with error flag or throw
  - Integrate `nSuspend_` counter increment/decrement

- [ ] **1.3** Implement `LocalValues` swap in new coroutine resume path
  - Before `handle.resume()`: save thread-local, install coroutine-local
  - After `handle.resume()` returns: restore thread-local
  - Ensure this works when coroutine migrates between threads

- [ ] **1.4** Add `postCoroTask()` template to `JobQueue`
  - Accept callable returning `CoroTask<void>`
  - Schedule initial execution on JobQueue (mirror `postCoro()` behavior)
  - Return a handle/shared_ptr for join/cancel

- [ ] **1.5** Write unit tests (`src/test/core/CoroTask_test.cpp`)
  - Test `CoroTask<void>` runs to completion
  - Test `CoroTask<int>` returns value
  - Test exception propagation across co_await
  - Test coroutine destruction before completion
  - Test `JobQueueAwaiter` schedules on correct thread
  - Test `LocalValue` isolation across 4+ coroutines
  - Test shutdown rejection (addJob returns false)
  - Test `correct_order` equivalent (yield → join → post → complete)
  - Test `incorrect_order` equivalent (post → yield → complete)
  - Test multiple sequential co_await points

- [ ] **1.6** Verify build on GCC 12+, Clang 16+
- [ ] **1.7** Run ASAN + TSAN on new tests
- [ ] **1.8** Run full `--unittest` suite (no regressions)
- [ ] **1.9** Self-review and create PR #1

---

## Milestone 2: Entry Point Migration

- [ ] **2.1** Migrate `ServerHandler::onRequest()` (`ServerHandler.cpp:287`)
  - Replace `m_jobQueue.postCoro(jtCLIENT_RPC, ...)` with `postCoroTask()`
  - Update lambda to return `CoroTask<void>` (add `co_return`)
  - Update `processSession` to accept new coroutine type

- [ ] **2.2** Migrate `ServerHandler::onWSMessage()` (`ServerHandler.cpp:325`)
  - Replace `m_jobQueue.postCoro(jtCLIENT_WEBSOCKET, ...)` with `postCoroTask()`
  - Update lambda signature

- [ ] **2.3** Migrate `GRPCServer::CallData::process()` (`GRPCServer.cpp:102`)
  - Replace `app_.getJobQueue().postCoro(JobType::jtRPC, ...)` with `postCoroTask()`
  - Update `process(shared_ptr<Coro> coro)` overload signature

- [ ] **2.4** Update `RPC::Context` (`Context.h:27`)
  - Replace `std::shared_ptr<JobQueue::Coro> coro{}` with new coroutine wrapper type
  - Ensure all code that accesses `context.coro` compiles

- [ ] **2.5** Update `ServerHandler.h` signatures
  - `processSession()` and `processRequest()` parameter types

- [ ] **2.6** Update `GRPCServer.h` signatures
  - `process()` method parameter types

- [ ] **2.7** Run full `--unittest` suite
- [ ] **2.8** Manual smoke test: HTTP + WS + gRPC RPC requests
- [ ] **2.9** Run ASAN + TSAN
- [ ] **2.10** Self-review and create PR #2

---

## Milestone 3: Handler Migration

- [ ] **3.1** Migrate `doRipplePathFind()` (`RipplePathFind.cpp`)
  - Replace `context.coro->yield()` with `co_await PathFindAwaiter{...}`
  - Replace continuation lambda's `coro->post()` / `coro->resume()` with awaiter scheduling
  - Handle shutdown case (post failure) in awaiter

- [ ] **3.2** Create `PathFindAwaiter` (or use generic `JobQueueAwaiter`)
  - Encapsulate the continuation + yield pattern from `RipplePathFind.cpp` lines 108-132

- [ ] **3.3** Update `Path_test.cpp`
  - Replace `postCoro` usage with `postCoroTask`
  - Ensure `context.coro` usage matches new type

- [ ] **3.4** Update `AMMTest.cpp`
  - Replace `postCoro` usage with `postCoroTask`

- [ ] **3.5** Rewrite `Coroutine_test.cpp` for new API
  - `correct_order`: postCoroTask → co_await → join → resume → complete
  - `incorrect_order`: post before yield equivalent
  - `thread_specific_storage`: 4 coroutines with LocalValue isolation

- [ ] **3.6** Update `JobQueue_test.cpp` `testPostCoro`
  - Migrate to `postCoroTask` API

- [ ] **3.7** Verify `ripple_path_find` works end-to-end with new coroutines
- [ ] **3.8** Test shutdown-during-pathfind scenario
- [ ] **3.9** Run full `--unittest` suite
- [ ] **3.10** Run ASAN + TSAN
- [ ] **3.11** Self-review and create PR #3

---

## Milestone 4: Cleanup & Validation

- [ ] **4.1** Delete `include/xrpl/core/Coro.ipp`
- [ ] **4.2** Remove from `JobQueue.h`:
  - `#include <boost/coroutine2/all.hpp>`
  - `struct Coro_create_t`
  - `class Coro` (entire class)
  - `postCoro()` template
  - Comment block (lines 322-377) describing old race condition
- [ ] **4.3** Update `cmake/deps/Boost.cmake`:
  - Remove `coroutine` from `find_package(Boost REQUIRED COMPONENTS ...)`
  - Remove `Boost::coroutine` from `target_link_libraries`
- [ ] **4.4** Update `cmake/XrplInterface.cmake`:
  - Remove `BOOST_COROUTINES2_NO_DEPRECATION_WARNING`
- [ ] **4.5** Run memory benchmark
  - Create N=1000 coroutines, compare RSS: before vs after
  - Document results
- [ ] **4.6** Run context switch benchmark
  - 100K yield/resume cycles, compare latency: before vs after
  - Document results
- [ ] **4.7** Run RPC throughput benchmark
  - Concurrent `ripple_path_find` requests, compare throughput
  - Document results
- [ ] **4.8** Run full `--unittest` suite
- [ ] **4.9** Run ASAN, TSAN, UBSan
  - Confirm `__asan_handle_no_return` warnings are gone
- [ ] **4.10** Verify build on all supported compilers
- [ ] **4.11** Self-review and create PR #4
- [ ] **4.12** Document final benchmark results in PR description
