#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/rdb/DatabaseCon.h>
#include <xrpl/rdb/SociDB.h>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include <memory>
#include <unordered_map>

namespace xrpl {

class CheckpointersCollection
{
    std::uintptr_t nextId_{0};
    // Mutex protects the CheckpointersCollection
    std::mutex mutex_;
    // Each checkpointer is given a unique id. All the checkpointers that are
    // part of a DatabaseCon are part of this collection. When the DatabaseCon
    // is destroyed, its checkpointer is removed from the collection
    std::unordered_map<std::uintptr_t, std::shared_ptr<Checkpointer>>
        checkpointers_;

public:
    std::shared_ptr<Checkpointer>
    fromId(std::uintptr_t id)
    {
        std::lock_guard l{mutex_};
        auto it = checkpointers_.find(id);
        if (it != checkpointers_.end())
            return it->second;
        return {};
    }

    void
    erase(std::uintptr_t id)
    {
        std::lock_guard lock{mutex_};
        checkpointers_.erase(id);
    }

    std::shared_ptr<Checkpointer>
    create(
        std::shared_ptr<soci::session> const& session,
        JobQueue& jobQueue,
        Logs& logs)
    {
        std::lock_guard lock{mutex_};
        auto const id = nextId_++;
        auto const r = makeCheckpointer(id, session, jobQueue, logs);
        checkpointers_[id] = r;
        return r;
    }
};

CheckpointersCollection checkpointers;

std::shared_ptr<Checkpointer>
checkpointerFromId(std::uintptr_t id)
{
    return checkpointers.fromId(id);
}

DatabaseCon::~DatabaseCon()
{
    if (checkpointer_)
    {
        checkpointers.erase(checkpointer_->id());

        std::weak_ptr<Checkpointer> wk(checkpointer_);
        checkpointer_.reset();

        // The references to our Checkpointer held by 'checkpointer_' and
        // 'checkpointers' have been removed, so if the use count is nonzero, a
        // checkpoint is currently in progress. Wait for it to end, otherwise
        // creating a new DatabaseCon to the same database may fail due to the
        // database being locked by our (now old) Checkpointer.
        while (wk.use_count())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

std::unique_ptr<std::vector<std::string> const>
    DatabaseCon::Setup::globalPragma;

void
DatabaseCon::setupCheckpointing(JobQueue* q, Logs& l)
{
    if (!q)
        Throw<std::logic_error>("No JobQueue");
    checkpointer_ = checkpointers.create(session_, *q, l);
}

}  // namespace xrpl
