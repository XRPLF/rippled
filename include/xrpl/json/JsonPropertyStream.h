#pragma once

#include <xrpl/beast/utility/PropertyStream.h>
#include <xrpl/json/json_value.h>

namespace xrpl {

/** A PropertyStream::Sink which produces a json::Value of type objectValue. */
class JsonPropertyStream : public beast::PropertyStream
{
public:
    json::Value topValue;  // TODO: rename: clashes with top() method
    std::vector<json::Value*> stack;

public:
    JsonPropertyStream();
    [[nodiscard]] json::Value const&
    top() const;

protected:
    void
    mapBegin() override;
    void
    mapBegin(std::string const& key) override;
    void
    mapEnd() override;
    void
    add(std::string const& key, short value) override;
    void
    add(std::string const& key, unsigned short value) override;
    void
    add(std::string const& key, int value) override;
    void
    add(std::string const& key, unsigned int value) override;
    void
    add(std::string const& key, long value) override;
    void
    add(std::string const& key, float v) override;
    void
    add(std::string const& key, double v) override;
    void
    add(std::string const& key, std::string const& v) override;
    void
    arrayBegin() override;
    void
    arrayBegin(std::string const& key) override;
    void
    arrayEnd() override;

    void
    add(short value) override;
    void
    add(unsigned short value) override;
    void
    add(int value) override;
    void
    add(unsigned int value) override;
    void
    add(long value) override;
    void
    add(float v) override;
    void
    add(double v) override;
    void
    add(std::string const& v) override;
};

}  // namespace xrpl
