#include <xrpl/basics/Log.h>
#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STArray.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/Serializer.h>

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace ripple {

STArray::STArray(STArray&& other)
    : STBase(other.getFName()), v_(std::move(other.v_))
{
}

STArray&
STArray::operator=(STArray&& other)
{
    setFName(other.getFName());
    v_ = std::move(other.v_);
    return *this;
}

STArray::STArray(int n)
{
    v_.reserve(n);
}

STArray::STArray(SField const& f) : STBase(f)
{
}

STArray::STArray(SField const& f, std::size_t n) : STBase(f)
{
    v_.reserve(n);
}

STArray::STArray(SerialIter& sit, SField const& f, int depth) : STBase(f)
{
    while (!sit.empty())
    {
        int type, field;
        sit.getFieldID(type, field);

        if ((type == STI_ARRAY) && (field == 1))
            break;

        if ((type == STI_OBJECT) && (field == 1))
        {
            JLOG(debugLog().error())
                << "Encountered array with end of object marker";
            Throw<std::runtime_error>("Illegal terminator in array");
        }

        auto const& fn = SField::getField(type, field);

        if (fn.isInvalid())
        {
            JLOG(debugLog().error())
                << "Unknown field: " << type << "/" << field;
            Throw<std::runtime_error>("Unknown field");
        }

        if (fn.fieldType != STI_OBJECT)
        {
            JLOG(debugLog().error()) << "Array contains non-object";
            Throw<std::runtime_error>("Non-object in array");
        }

        v_.emplace_back(sit, fn, depth + 1);

        v_.back().applyTemplateFromSField(fn);  // May throw
    }
}

STBase*
STArray::copy(std::size_t n, void* buf) const
{
    return emplace(n, buf, *this);
}

STBase*
STArray::move(std::size_t n, void* buf)
{
    return emplace(n, buf, std::move(*this));
}

std::string
STArray::getFullText() const
{
    std::string r = "[";

    bool first = true;
    for (auto const& obj : v_)
    {
        if (!first)
            r += ",";

        r += obj.getFullText();
        first = false;
    }

    r += "]";
    return r;
}

std::string
STArray::getText() const
{
    std::string r = "[";

    bool first = true;
    for (STObject const& o : v_)
    {
        if (!first)
            r += ",";

        r += o.getText();
        first = false;
    }

    r += "]";
    return r;
}

Json::Value
STArray::getJson(JsonOptions p) const
{
    Json::Value v = Json::arrayValue;
    for (auto const& object : v_)
    {
        if (object.getSType() != STI_NOTPRESENT)
        {
            Json::Value& inner = v.append(Json::objectValue);
            inner[object.getFName().getJsonName()] = object.getJson(p);
        }
    }
    return v;
}

void
STArray::add(Serializer& s) const
{
    for (STObject const& object : v_)
    {
        object.addFieldID(s);
        object.add(s);
        s.addFieldID(STI_OBJECT, 1);
    }
}

SerializedTypeID
STArray::getSType() const
{
    return STI_ARRAY;
}

bool
STArray::isEquivalent(STBase const& t) const
{
    auto v = dynamic_cast<STArray const*>(&t);
    return v != nullptr && v_ == v->v_;
}

bool
STArray::isDefault() const
{
    return v_.empty();
}

void
STArray::sort(bool (*compare)(STObject const&, STObject const&))
{
    std::sort(v_.begin(), v_.end(), compare);
}

}  // namespace ripple
