#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Disposition.h>
#include <xrpl/resource/detail/Entry.h>
#include <xrpl/resource/detail/Logic.h>

#include <ostream>
#include <string>

namespace xrpl {
namespace Resource {

Consumer::Consumer(Logic& logic, Entry& entry) : logic_(&logic), entry_(&entry)
{
}

Consumer::Consumer() : logic_(nullptr), entry_(nullptr)
{
}

Consumer::Consumer(Consumer const& other) : logic_(other.logic_), entry_(nullptr)
{
    if (logic_ && other.entry_)
    {
        entry_ = other.entry_;
        logic_->acquire(*entry_);
    }
}

Consumer::~Consumer()
{
    if (logic_ && entry_)
        logic_->release(*entry_);
}

Consumer&
Consumer::operator=(Consumer const& other)
{
    if (this == &other)
        return *this;

    // remove old ref
    if (logic_ && entry_)
        logic_->release(*entry_);

    logic_ = other.logic_;
    entry_ = other.entry_;

    // add new ref
    if (logic_ && entry_)
        logic_->acquire(*entry_);

    return *this;
}

std::string
Consumer::to_string() const
{
    if (logic_ == nullptr)
        return "(none)";

    return entry_->to_string();
}

bool
Consumer::isUnlimited() const
{
    if (entry_)
        return entry_->isUnlimited();

    return false;
}

Disposition
Consumer::disposition() const
{
    Disposition d = ok;
    if (logic_ && entry_)
        d = logic_->charge(*entry_, Charge(0));

    return d;
}

Disposition
Consumer::charge(Charge const& what, std::string const& context)
{
    Disposition d = ok;

    if (logic_ && entry_ && !entry_->isUnlimited())
        d = logic_->charge(*entry_, what, context);

    return d;
}

bool
Consumer::warn()
{
    XRPL_ASSERT(entry_, "xrpl::Resource::Consumer::warn : non-null entry");
    return logic_->warn(*entry_);
}

bool
Consumer::disconnect(beast::Journal const& j)
{
    XRPL_ASSERT(entry_, "xrpl::Resource::Consumer::disconnect : non-null entry");
    bool const d = logic_->disconnect(*entry_);
    if (d)
    {
        JLOG(j.debug()) << "disconnecting " << entry_->to_string();
    }
    return d;
}

int
Consumer::balance()
{
    XRPL_ASSERT(entry_, "xrpl::Resource::Consumer::balance : non-null entry");
    return logic_->balance(*entry_);
}

Entry&
Consumer::entry()
{
    XRPL_ASSERT(entry_, "xrpl::Resource::Consumer::entry : non-null entry");
    return *entry_;
}

void
Consumer::setPublicKey(PublicKey const& publicKey)
{
    entry_->publicKey = publicKey;
}

std::ostream&
operator<<(std::ostream& os, Consumer const& v)
{
    os << v.to_string();
    return os;
}

}  // namespace Resource
}  // namespace xrpl
