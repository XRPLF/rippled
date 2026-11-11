#pragma once

#include <concepts>
#include <functional>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>

namespace xrpl {

/**
 * @brief A value that is lazily initialized exactly once in a thread-safe way.
 *
 * @tparam T The type of the stored value
 */
template <typename T>
class Once
{
    std::optional<T> data_;
    std::once_flag flag_;

public:
    /**
     * @brief Get the stored value, initializing it with the given function on the first call.
     *
     * If several threads call this concurrently, exactly one of them runs @p init and the others
     * block until it finishes. If @p init throws, the exception is propagated, the value stays
     * uninitialized and the next call will try to initialize it again.
     *
     * @note Only the initialization is synchronized. Access to the value through the returned
     * reference must be synchronized by the caller. Calling getOrInit() on the same object from
     * within @p init deadlocks.
     *
     * @param init The function producing the value. Called only if the value is not initialized.
     * @return Reference to the stored value
     */
    template <std::invocable F>
        requires std::convertible_to<std::invoke_result_t<F>, T>
    T&
    getOrInit(F&& init)
    {
        std::call_once(flag_, [&] { data_.emplace(std::invoke(std::forward<F>(init))); });
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) call_once returns only after emplace
        return *data_;
    }
};

}  // namespace xrpl
