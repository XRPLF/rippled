#include <xrpl/basics/NameRegistry.h>

#include <xrpl/beast/utility/instrumentation.h>

#include <cassert>
#include <cstddef>
#include <mutex>
#include <stdexcept>
#include <string>

namespace xrpl {

void
NameRegistry::add(std::string const& name)
{
    if (name.empty())
        throw std::invalid_argument("NameRegistry::add : empty name");

    std::scoped_lock const lock(mutex_);
    names_.push_back(name);
}

void
NameRegistry::greeting(std::size_t index, Style style, std::string& out) const
{
    std::scoped_lock const lock(mutex_);
    assert(index < names_.size());

    switch (style)
    {
        case Style::Formal:
            out = "Dear " + names_[index] + ", you are visitor number " +
                std::to_string(index + 1) + ".";
            return;
        case Style::Casual:
            out = "Hi " + names_[index] + "!";
            return;
    }
    UNREACHABLE("NameRegistry::greeting : invalid style");
}

std::string
NameRegistry::find(std::string const& prefix) const
{
    std::scoped_lock const lock(mutex_);
    for (auto const& name : names_)
    {
        if (name.starts_with(prefix))
            return name;
    }
    return {};
}

std::size_t
NameRegistry::size() const
{
    std::scoped_lock const lock(mutex_);
    return names_.size();
}

}  // namespace xrpl
