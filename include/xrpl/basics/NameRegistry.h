#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace xrpl {

class NameRegistry
{
public:
    enum class Style { Formal, Casual };

    // Throws std::invalid_argument if name is empty.
    void
    add(std::string const& name);

    void
    greeting(std::size_t index, Style style, std::string& out) const;

    // Returns an empty string if no name starts with prefix.
    [[nodiscard]] std::string
    find(std::string const& prefix) const;

    [[nodiscard]] std::size_t
    size() const;

private:
    mutable std::mutex mutex_;
    std::vector<std::string> names_;
};

}  // namespace xrpl
