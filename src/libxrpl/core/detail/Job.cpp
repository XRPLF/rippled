#include <xrpl/core/Job.h>

#include <xrpl/beast/core/CurrentThreadName.h>
#include <xrpl/core/LoadEvent.h>
#include <xrpl/core/LoadMonitor.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace xrpl {

Job::Job() : type_(JtInvalid), jobIndex_(0)
{
}

Job::Job(JobType type, std::uint64_t index) : type_(type), jobIndex_(index)
{
}

Job::Job(
    JobType type,
    std::string const& name,
    std::uint64_t index,
    LoadMonitor& lm,
    std::function<void()> const& job)
    : type_(type), jobIndex_(index), job_(job), name_(name), queueTime_(clock_type::now())
{
    loadEvent_ = std::make_shared<LoadEvent>(std::ref(lm), name, false);
}

JobType
Job::getType() const
{
    return type_;
}

Job::clock_type::time_point const&
Job::queueTime() const
{
    return queueTime_;
}

void
Job::doJob()
{
    beast::setCurrentThreadName("j:" + name_);
    loadEvent_->start();
    loadEvent_->setName(name_);

    job_();

    // Destroy the lambda, otherwise we won't include
    // its duration in the time measurement
    job_ = nullptr;
}

bool
Job::operator>(Job const& j) const
{
    if (type_ < j.type_)
        return true;

    if (type_ > j.type_)
        return false;

    return jobIndex_ > j.jobIndex_;
}

bool
Job::operator>=(Job const& j) const
{
    if (type_ < j.type_)
        return true;

    if (type_ > j.type_)
        return false;

    return jobIndex_ >= j.jobIndex_;
}

bool
Job::operator<(Job const& j) const
{
    if (type_ < j.type_)
        return false;

    if (type_ > j.type_)
        return true;

    return jobIndex_ < j.jobIndex_;
}

bool
Job::operator<=(Job const& j) const
{
    if (type_ < j.type_)
        return false;

    if (type_ > j.type_)
        return true;

    return jobIndex_ <= j.jobIndex_;
}

}  // namespace xrpl
