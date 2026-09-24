#include <xrpl/json/json_reader.h>

#include <xrpl/basics/contract.h>
#include <xrpl/json/json_value.h>

#include <cstddef>
#include <expected>
#include <istream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace json {

// Class Reader::ValueBuilder
// //////////////////////////////////////////////////////////////////

void
Reader::ValueBuilder::target(Value& root)
{
    root_ = &root;
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onDocumentBegin()
{
    while (!nodes_.empty())
    {
        nodes_.pop();
    }

    pendingKey_.clear();
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onDocumentEnd(std::size_t)
{
    // A document is an object, an array, or null. A bare scalar is rejected
    // even though it is well formed JSON.
    if ((root_ != nullptr) && !root_->isNull() && !root_->isArray() && !root_->isObject())
    {
        return std::unexpected(
            std::string{"A valid JSON document must be either an array or an object value."});
    }

    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onObjectBegin()
{
    Value& value = place();
    value = Value(ValueType::Object);
    nodes_.push(&value);
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onObjectEnd(std::size_t)
{
    nodes_.pop();
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onArrayBegin()
{
    Value& value = place();
    value = Value(ValueType::Array);
    nodes_.push(&value);
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onArrayEnd(std::size_t)
{
    nodes_.pop();
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onKey(std::string_view key)
{
    std::string name{key};

    // Reject duplicate names. The enclosing object already holds every member
    // seen so far, so no separate bookkeeping is needed.
    if (nodes_.top()->isMember(name))
    {
        return std::unexpected("Key '" + name + "' appears twice.");
    }

    pendingKey_ = std::move(name);
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onString(std::string_view value)
{
    place() = std::string{value};
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onInt(Value::Int value)
{
    place() = value;
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onUInt(Value::UInt value)
{
    place() = value;
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onDouble(double value)
{
    place() = value;
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onBool(bool value)
{
    place() = value;
    return {};
}

Reader::ValueBuilder::ReturnType
Reader::ValueBuilder::onNull()
{
    place() = Value();
    return {};
}

Value&
Reader::ValueBuilder::place()
{
    if (nodes_.empty())
    {
        return *root_;
    }

    Value& parent = *nodes_.top();

    if (parent.isObject())
    {
        // The pending key is always consumed by the very next value, so a
        // single slot suffices however deeply objects nest.
        Value& slot = parent[pendingKey_];
        pendingKey_.clear();
        return slot;
    }

    return parent[parent.size()];
}

// Class Reader
// //////////////////////////////////////////////////////////////////

Reader::Reader()
{
    parser_.depthLimit = kNestLimit;
}

Reader::Reader(Reader&& other) noexcept
    : builder_{std::move(other.builder_)}, parser_{std::move(other.parser_)}
{
    parser_.visitors(builder_);
}

Reader&
Reader::operator=(Reader&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    builder_ = std::move(other.builder_);
    parser_ = std::move(other.parser_);
    parser_.visitors(builder_);

    return *this;
}

bool
Reader::parse(std::string const& document, Value& root)
{
    builder_.target(root);
    return parser_.parse(document);
}

bool
Reader::parse(char const* beginDoc, char const* endDoc, Value& root)
{
    builder_.target(root);
    return parser_.parse(beginDoc, endDoc);
}

bool
Reader::parse(std::istream& sin, Value& root)
{
    builder_.target(root);
    return parser_.parse(sin);
}

std::string
Reader::getFormattedErrorMessages() const
{
    return parser_.getFormattedErrorMessages();
}

std::istream&
operator>>(std::istream& sin, Value& root)
{
    json::Reader reader;
    bool const ok = reader.parse(sin, root);

    // XRPL_ASSERT(ok, "json::operator>>() : parse succeeded");
    if (!ok)
    {
        xrpl::Throw<std::runtime_error>(reader.getFormattedErrorMessages());
    }

    return sin;
}

}  // namespace json
