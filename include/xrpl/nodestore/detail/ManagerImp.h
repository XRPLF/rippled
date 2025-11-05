#ifndef XRPL_NODESTORE_MANAGERIMP_H_INCLUDED
#define XRPL_NODESTORE_MANAGERIMP_H_INCLUDED

#include <xrpl/nodestore/Manager.h>

namespace ripple {

namespace NodeStore {

class ManagerImp : public Manager
{
private:
    std::mutex mutex_;
    std::vector<Factory*> list_;

public:
    static ManagerImp&
    instance();

    static void
    missing_backend();

    ManagerImp();

    ~ManagerImp() = default;

    Factory*
    find(std::string const& name) override;

    void
    insert(Factory& factory) override;

    void
    erase(Factory& factory) override;

    std::unique_ptr<Backend>
    make_Backend(
        Section const& parameters,
        std::size_t burstSize,
        Scheduler& scheduler,
        beast::Journal journal) override;

    std::unique_ptr<Database>
    make_Database(
        std::size_t burstSize,
        Scheduler& scheduler,
        int readThreads,
        Section const& config,
        beast::Journal journal) override;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
