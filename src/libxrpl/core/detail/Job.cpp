#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/core/Job.h>

namespace xrpl {

Job::Job() : mType(jtINVALID), mJobIndex(0)
{
}

Job::Job(JobType type, std::uint64_t index) : mType(type), mJobIndex(index)
{
}

Job::Job(
    JobType type,
    std::string const& name,
    std::uint64_t index,
    LoadMonitor& lm,
    std::function<void()> const& job)
    : mType(type), mJobIndex(index), mJob(job), mName(name), queue_time_(clock_type::now())
{
    loadEvent_ = std::make_shared<LoadEvent>(std::ref(lm), name, false);
}

JobType
Job::getType() const
{
    return mType;
}

Job::clock_type::time_point const&
Job::queue_time() const
{
    return queue_time_;
}

void
Job::doJob()
{
    beast::setCurrentThreadName("j:" + mName);
    loadEvent_->start();
    loadEvent_->setName(mName);

    mJob();

    // Destroy the lambda, otherwise we won't include
    // its duration in the time measurement
    mJob = nullptr;
}

bool
Job::operator>(Job const& j) const
{
    if (mType < j.mType)
        return true;

    if (mType > j.mType)
        return false;

    return mJobIndex > j.mJobIndex;
}

bool
Job::operator>=(Job const& j) const
{
    if (mType < j.mType)
        return true;

    if (mType > j.mType)
        return false;

    return mJobIndex >= j.mJobIndex;
}

bool
Job::operator<(Job const& j) const
{
    if (mType < j.mType)
        return false;

    if (mType > j.mType)
        return true;

    return mJobIndex < j.mJobIndex;
}

bool
Job::operator<=(Job const& j) const
{
    if (mType < j.mType)
        return false;

    if (mType > j.mType)
        return true;

    return mJobIndex <= j.mJobIndex;
}

}  // namespace xrpl
