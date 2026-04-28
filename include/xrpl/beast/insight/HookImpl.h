#pragma once

#include <functional>
#include <memory>

namespace beast::insight {

class HookImpl : public std::enable_shared_from_this<HookImpl>
{
public:
    using HandlerType = std::function<void(void)>;

    virtual ~HookImpl() = 0;
};

}  // namespace beast::insight
