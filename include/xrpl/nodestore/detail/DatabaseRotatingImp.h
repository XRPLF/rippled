#ifndef XRPL_NODESTORE_DATABASEROTATINGIMP_H_INCLUDED
#define XRPL_NODESTORE_DATABASEROTATINGIMP_H_INCLUDED

#include <xrpl/nodestore/DatabaseRotating.h>

#include <mutex>

namespace ripple {
namespace NodeStore {

class DatabaseRotatingImp : public DatabaseRotating
{
public:
    DatabaseRotatingImp() = delete;
    DatabaseRotatingImp(DatabaseRotatingImp const&) = delete;
    DatabaseRotatingImp&
    operator=(DatabaseRotatingImp const&) = delete;

    DatabaseRotatingImp(
        Scheduler& scheduler,
        int readThreads,
        std::shared_ptr<Backend> writableBackend,
        std::shared_ptr<Backend> archiveBackend,
        Section const& config,
        beast::Journal j);

    ~DatabaseRotatingImp()
    {
        stop();
    }

    void
    rotate(
        std::unique_ptr<NodeStore::Backend>&& newBackend,
        std::function<void(
            std::string const& writableName,
            std::string const& archiveName)> const& f) override;

    std::string
    getName() const override;

    std::int32_t
    getWriteLoad() const override;

    void
    importDatabase(Database& source) override;

    bool
    isSameDB(std::uint32_t, std::uint32_t) override
    {
        // rotating store acts as one logical database
        return true;
    }

    void
    store(NodeObjectType type, Blob&& data, uint256 const& hash, std::uint32_t)
        override;

    void
    sync() override;

    void
    sweep() override;

private:
    std::shared_ptr<Backend> writableBackend_;
    std::shared_ptr<Backend> archiveBackend_;
    mutable std::mutex mutex_;

    std::shared_ptr<NodeObject>
    fetchNodeObject(
        uint256 const& hash,
        std::uint32_t,
        FetchReport& fetchReport,
        bool duplicate) override;

    void
    for_each(std::function<void(std::shared_ptr<NodeObject>)> f) override;
};

}  // namespace NodeStore
}  // namespace ripple

#endif
