// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#pragma once

#include <xrpl/beast/unit_test/suite.h>

#include <functional>
#include <thread>
#include <utility>

namespace beast::unit_test {

/** Replacement for std::thread that handles exceptions in unit tests. */
class Thread
{
private:
    suite* s_ = nullptr;
    std::thread t_;

public:
    using id = std::thread::id;
    using native_handle_type = std::thread::native_handle_type;

    Thread() = default;
    Thread(Thread const&) = delete;
    Thread&
    operator=(Thread const&) = delete;

    Thread(Thread&& other) : s_(other.s_), t_(std::move(other.t_))
    {
    }

    Thread&
    operator=(Thread&& other)
    {
        s_ = other.s_;
        t_ = std::move(other.t_);
        return *this;
    }

    template <class F, class... Args>
    explicit Thread(suite& s, F&& f, Args&&... args) : s_(&s)
    {
        std::function<void(void)> b = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        t_ = std::thread(&Thread::run, this, std::move(b));
    }

    [[nodiscard]] bool
    joinable() const
    {
        return t_.joinable();
    }

    [[nodiscard]] std::thread::id
    get_id() const
    {
        return t_.get_id();
    }

    static unsigned
    hardware_concurrency() noexcept
    {
        return std::thread::hardware_concurrency();
    }

    void
    join()
    {
        t_.join();
        s_->propagate_abort();
    }

    void
    detach()
    {
        t_.detach();
    }

    void
    swap(Thread& other)
    {
        std::swap(s_, other.s_);
        std::swap(t_, other.t_);
    }

private:
    void
    run(std::function<void(void)> f)
    {
        try
        {
            f();
        }
        catch (suite::abort_exception const&)  // NOLINT(bugprone-empty-catch)
        {
        }
        catch (std::exception const& e)
        {
            s_->fail("unhandled exception: " + std::string(e.what()));
        }
        catch (...)
        {
            s_->fail("unhandled exception");
        }
    }
};

}  // namespace beast::unit_test
