#pragma once

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/nodestore/Backend.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/Factory.h>
#include <xrpl/nodestore/Manager.h>
#include <xrpl/nodestore/Scheduler.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace xrpl::NodeStore {

class ManagerImp : public Manager
{
private:
    std::mutex mutex_;
    std::vector<Factory*> list_;

public:
    static ManagerImp&
    instance();

    static void
    missingBackend();

    ManagerImp();

    ~ManagerImp() override = default;

    Factory*
    find(std::string const& name) override;

    void
    insert(Factory& factory) override;

    void
    erase(Factory& factory) override;

    std::unique_ptr<Backend>
    makeBackend(
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override;

    std::unique_ptr<Database>
    makeDatabase(
        std::size_t burstSize,
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal) override;
};

}  // namespace xrpl::NodeStore
