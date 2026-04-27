#pragma once

#include <xrpl/beast/insight/Collector.h>

#include <memory>
#include <string>

namespace beast::insight {

/** A collector front-end that manages a group of metrics. */
class Group : public Collector
{
public:
    using ptr = std::shared_ptr<Group>;

    /** Returns the name of this group, for diagnostics. */
    [[nodiscard]] virtual std::string const&
    name() const = 0;
};

}  // namespace beast::insight
