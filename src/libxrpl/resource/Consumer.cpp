#include <xrpl/resource/Consumer.h>

#include <xrpl/basics/Log.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Disposition.h>
#include <xrpl/resource/detail/Entry.h>
#include <xrpl/resource/detail/Logic.h>

#include <ostream>
#include <string>

namespace xrpl::Resource {

Consumer::Consumer(Logic& logic, Entry& entry) : logic_(&logic), entry_(&entry)
{
}

Consumer::Consumer() : logic_(nullptr), entry_(nullptr)
{
}

Consumer::Consumer(Consumer const& other) : logic_(other.logic_), entry_(nullptr)
{
    if ((logic_ != nullptr) && (other.entry_ != nullptr))
    {
        entry_ = other.entry_;
        logic_->acquire(*entry_);
    }
}

Consumer::~Consumer()
{
    if ((logic_ != nullptr) && (entry_ != nullptr))
        logic_->release(*entry_);
}

Consumer&
Consumer::operator=(Consumer const& other)
{
    if (this == &other)
        return *this;

    // remove old ref
    if ((logic_ != nullptr) && (entry_ != nullptr))
        logic_->release(*entry_);

    logic_ = other.logic_;
    entry_ = other.entry_;

    // add new ref
    if ((logic_ != nullptr) && (entry_ != nullptr))
        logic_->acquire(*entry_);

    return *this;
}

std::string
Consumer::toString() const
{
    if (logic_ == nullptr)
        return "(none)";

    return entry_->toString();
}

bool
Consumer::isUnlimited() const
{
    if (entry_ != nullptr)
        return entry_->isUnlimited();

    return false;
}

Disposition
Consumer::disposition() const
{
    Disposition d = Disposition::Ok;
    if ((logic_ != nullptr) && (entry_ != nullptr))
        d = logic_->charge(*entry_, Charge(0));

    return d;
}

Disposition
Consumer::charge(Charge const& what, std::string const& context)
{
    Disposition d = Disposition::Ok;

    if ((logic_ != nullptr) && (entry_ != nullptr) && !entry_->isUnlimited())
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
        JLOG(j.debug()) << "disconnecting " << entry_->toString();
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
    os << v.toString();
    return os;
}

}  // namespace xrpl::Resource
