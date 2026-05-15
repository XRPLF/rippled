#include <xrpl/nodestore/detail/BatchWriter.h>

#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/nodestore/Scheduler.h>
#include <xrpl/nodestore/Types.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

namespace xrpl::NodeStore {

BatchWriter::BatchWriter(Callback& callback, Scheduler& scheduler)
    : callback_(callback), scheduler_(scheduler)
{
    writeSet_.reserve(kBatchWritePreallocationSize);
}

BatchWriter::~BatchWriter()
{
    waitForWriting();
}

void
BatchWriter::store(std::shared_ptr<NodeObject> const& object)
{
    std::unique_lock<decltype(writeMutex_)> sl(writeMutex_);

    // If the batch has reached its limit, we wait
    // until the batch writer is finished
    while (writeSet_.size() >= kBatchWritePreallocationSize)
        writeCondition_.wait(sl);

    writeSet_.push_back(object);

    if (!writePending_)
    {
        writePending_ = true;

        scheduler_.scheduleTask(*this);
    }
}

int
BatchWriter::getWriteLoad()
{
    std::scoped_lock const sl(writeMutex_);

    return std::max(writeLoad_, static_cast<int>(writeSet_.size()));
}

void
BatchWriter::performScheduledTask()
{
    writeBatch();
}

void
BatchWriter::writeBatch()
{
    for (;;)
    {
        std::vector<std::shared_ptr<NodeObject>> set;

        set.reserve(kBatchWritePreallocationSize);

        {
            std::scoped_lock const sl(writeMutex_);

            writeSet_.swap(set);
            XRPL_ASSERT(
                writeSet_.empty(), "xrpl::NodeStore::BatchWriter::writeBatch : writes not set");
            writeLoad_ = set.size();

            if (set.empty())
            {
                writePending_ = false;
                writeCondition_.notify_all();

                // VFALCO NOTE Fix this function to not return from the middle
                return;
            }
        }

        BatchWriteReport report{};
        report.writeCount = set.size();
        auto const before = std::chrono::steady_clock::now();

        callback_.writeBatch(set);

        report.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - before);

        scheduler_.onBatchWrite(report);
    }
}

void
BatchWriter::waitForWriting()
{
    std::unique_lock<decltype(writeMutex_)> sl(writeMutex_);

    while (writePending_)
        writeCondition_.wait(sl);
}

}  // namespace xrpl::NodeStore
