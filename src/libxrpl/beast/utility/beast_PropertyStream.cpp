#include <xrpl/beast/core/List.h>
#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/beast/utility/instrumentation.h>

#include <algorithm>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>

namespace beast {

//------------------------------------------------------------------------------
//
// Item
//
//------------------------------------------------------------------------------

PropertyStream::Item::Item(Source* source) : source_(source)
{
}

PropertyStream::Source&
PropertyStream::Item::source() const
{
    return *source_;
}

PropertyStream::Source*
PropertyStream::Item::operator->() const
{
    return &source();
}

PropertyStream::Source&
PropertyStream::Item::operator*() const
{
    return source();
}

//------------------------------------------------------------------------------
//
// Proxy
//
//------------------------------------------------------------------------------

PropertyStream::Proxy::Proxy(Map const& map, std::string key) : map_(&map), key_(std::move(key))
{
}

PropertyStream::Proxy::Proxy(Proxy const& other) : map_(other.map_), key_(other.key_)
{
}

PropertyStream::Proxy::~Proxy()
{
    std::string const s(ostream_.str());
    if (!s.empty())
        map_->add(key_, s);
}

std::ostream&
PropertyStream::Proxy::operator<<(std::ostream& manip(std::ostream&)) const
{
    return ostream_ << manip;
}

//------------------------------------------------------------------------------
//
// Map
//
//------------------------------------------------------------------------------

PropertyStream::Map::Map(PropertyStream& stream) : stream_(stream)
{
}

PropertyStream::Map::Map(Set& parent) : stream_(parent.stream())
{
    stream_.mapBegin();
}

PropertyStream::Map::Map(std::string const& key, Map& map) : stream_(map.stream())
{
    stream_.mapBegin(key);
}

PropertyStream::Map::Map(std::string const& key, PropertyStream& stream) : stream_(stream)
{
    stream_.mapBegin(key);
}

PropertyStream::Map::~Map()
{
    stream_.mapEnd();
}

PropertyStream&
PropertyStream::Map::stream()
{
    return stream_;
}

PropertyStream const&
PropertyStream::Map::stream() const
{
    return stream_;
}

PropertyStream::Proxy
PropertyStream::Map::operator[](std::string const& key)
{
    return Proxy(*this, key);
}

//------------------------------------------------------------------------------
//
// Set
//
//------------------------------------------------------------------------------

PropertyStream::Set::Set(std::string const& key, Map& map) : stream_(map.stream())
{
    stream_.arrayBegin(key);
}

PropertyStream::Set::Set(std::string const& key, PropertyStream& stream) : stream_(stream)
{
    stream_.arrayBegin(key);
}

PropertyStream::Set::~Set()
{
    stream_.arrayEnd();
}

PropertyStream&
PropertyStream::Set::stream()
{
    return stream_;
}

PropertyStream const&
PropertyStream::Set::stream() const
{
    return stream_;
}

//------------------------------------------------------------------------------
//
// Source
//
//------------------------------------------------------------------------------

PropertyStream::Source::Source(std::string name) : name_(std::move(name)), item_(this)
{
}

PropertyStream::Source::~Source()
{
    std::scoped_lock const _(lock_);
    if (parent_ != nullptr)
        parent_->remove(*this);
    removeAll();
}

std::string const&
PropertyStream::Source::name() const
{
    return name_;
}

void
PropertyStream::Source::add(Source& source)
{
    std::scoped_lock const lock(lock_, source.lock_);

    XRPL_ASSERT(
        source.parent_ == nullptr, "beast::PropertyStream::Source::add : null source parent");
    children_.pushBack(source.item_);
    source.parent_ = this;
}

void
PropertyStream::Source::remove(Source& child)
{
    std::scoped_lock const lock(lock_, child.lock_);

    XRPL_ASSERT(
        child.parent_ == this, "beast::PropertyStream::Source::remove : child parent match");
    children_.erase(children_.iteratorTo(child.item_));
    child.parent_ = nullptr;
}

void
PropertyStream::Source::removeAll()
{
    std::scoped_lock const _(lock_);
    for (auto iter = children_.begin(); iter != children_.end();)
    {
        std::scoped_lock const cl((*iter)->lock_);
        remove(*(*iter));
    }
}

//------------------------------------------------------------------------------

void
PropertyStream::Source::writeOne(PropertyStream& stream)
{
    Map map(name_, stream);
    onWrite(map);
}

void
PropertyStream::Source::write(PropertyStream& stream)
{
    Map map(name_, stream);
    onWrite(map);

    std::scoped_lock const _(lock_);

    for (auto& child : children_)
        child.source().write(stream);
}

void
PropertyStream::Source::write(PropertyStream& stream, std::string const& path)
{
    std::pair<Source*, bool> result(find(path));

    if (result.first == nullptr)
        return;

    if (result.second)
    {
        result.first->write(stream);
    }
    else
    {
        result.first->writeOne(stream);
    }
}

std::pair<PropertyStream::Source*, bool>
PropertyStream::Source::find(std::string path)
{
    bool const deep(peelTrailingSlashstar(&path));
    bool const rooted(peelLeadingSlash(&path));
    Source* source(this);
    if (!path.empty())
    {
        if (!rooted)
        {
            std::string const name(peelName(&path));
            source = findOneDeep(name);
            if (source == nullptr)
                return std::make_pair(nullptr, deep);
        }
        source = source->findPath(path);
    }
    return std::make_pair(source, deep);
}

bool
PropertyStream::Source::peelLeadingSlash(std::string* path)
{
    if (!path->empty() && path->front() == '/')
    {
        *path = std::string(path->begin() + 1, path->end());
        return true;
    }
    return false;
}

bool
PropertyStream::Source::peelTrailingSlashstar(std::string* path)
{
    bool found(false);
    if (path->empty())
        return false;
    if (path->back() == '*')
    {
        found = true;
        path->pop_back();
    }
    if (!path->empty() && path->back() == '/')
        path->pop_back();
    return found;
}

std::string
PropertyStream::Source::peelName(std::string* path)
{
    if (path->empty())
        return "";

    std::string::const_iterator const first = (*path).begin();
    std::string::const_iterator const last = (*path).end();
    std::string::const_iterator const pos = std::find(first, last, '/');
    std::string s(first, pos);

    if (pos != last)
    {
        *path = std::string(pos + 1, last);
    }
    else
    {
        *path = std::string();
    }

    return s;
}

// Recursive search through the whole tree until name is found
PropertyStream::Source*
PropertyStream::Source::findOneDeep(std::string const& name)
{
    Source* found = findOne(name);  // NOLINT TODO
    if (found != nullptr)
        return found;

    std::scoped_lock const _(lock_);
    for (auto& s : children_)
    {
        found = s.source().findOneDeep(name);
        if (found != nullptr)
            return found;
    }
    return nullptr;
}

PropertyStream::Source*
PropertyStream::Source::findPath(std::string path)
{
    if (path.empty())
        return this;
    Source* source(this);
    do
    {
        std::string const name(peelName(&path));
        if (name.empty())
            break;
        source = source->findOne(name);
    } while (source != nullptr);
    return source;
}

// This function only looks at immediate children
// If no immediate children match, then return nullptr
PropertyStream::Source*
PropertyStream::Source::findOne(std::string const& name)
{
    std::scoped_lock const _(lock_);
    for (auto& s : children_)
    {
        if (s.source().name_ == name)
            return &s.source();
    }
    return nullptr;
}

void
PropertyStream::Source::onWrite(Map&)
{
}

//------------------------------------------------------------------------------
//
// PropertyStream
//
//------------------------------------------------------------------------------

void
PropertyStream::add(std::string const& key, bool value)
{
    if (value)
    {
        add(key, "true");
    }
    else
    {
        add(key, "false");
    }
}

void
PropertyStream::add(std::string const& key, char value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, signed char value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, unsigned char value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, short value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, unsigned short value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, int value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, unsigned int value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, long value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, unsigned long value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, long long value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, unsigned long long value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, float value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, double value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(std::string const& key, long double value)
{
    lexicalAdd(key, value);
}

void
PropertyStream::add(bool value)
{
    if (value)
    {
        add("true");
    }
    else
    {
        add("false");
    }
}

void
PropertyStream::add(char value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(signed char value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(unsigned char value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(short value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(unsigned short value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(int value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(unsigned int value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(long value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(unsigned long value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(long long value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(unsigned long long value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(float value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(double value)
{
    lexicalAdd(value);
}

void
PropertyStream::add(long double value)
{
    lexicalAdd(value);
}

}  // namespace beast
