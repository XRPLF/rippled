#pragma once

#include <chrono>
#include <memory>

namespace beast::insight {

class Event;

class EventImpl : public std::enable_shared_from_this<EventImpl>
{
public:
    using ValueType = std::chrono::milliseconds;

    virtual ~EventImpl() = 0;
    virtual void
    notify(ValueType const& value) = 0;
};

}  // namespace beast::insight
