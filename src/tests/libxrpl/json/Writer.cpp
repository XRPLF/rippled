#include <xrpl/json/Writer.h>

#include <xrpl/json/Output.h>
#include <xrpl/json/json_value.h>

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace xrpl;
using namespace json;

class WriterFixture : public ::testing::Test
{
protected:
    std::string output_;
    std::unique_ptr<Writer> writer_;

    void
    SetUp() override
    {
        writer_ = std::make_unique<Writer>(stringOutput(output_));
    }

    void
    reset()
    {
        output_.clear();
        writer_ = std::make_unique<Writer>(stringOutput(output_));
    }

    void
    expectOutput(std::string const& expected) const
    {
        EXPECT_EQ(output_, expected);
    }

    void
    checkOutputAndReset(std::string const& expected)
    {
        expectOutput(expected);
        reset();
    }
};

TEST_F(WriterFixture, trivial)
{
    EXPECT_TRUE(output_.empty());
    checkOutputAndReset("");
}

TEST_F(WriterFixture, near_trivial)
{
    EXPECT_TRUE(output_.empty());
    writer_->output(0);
    checkOutputAndReset("0");
}

TEST_F(WriterFixture, primitives)
{
    writer_->output(true);
    checkOutputAndReset("true");

    writer_->output(false);
    checkOutputAndReset("false");

    writer_->output(23);
    checkOutputAndReset("23");

    writer_->output(23.0);
    checkOutputAndReset("23.0");

    writer_->output(23.5);
    checkOutputAndReset("23.5");

    writer_->output("a string");
    checkOutputAndReset(R"("a string")");

    writer_->output(nullptr);
    checkOutputAndReset("null");
}

TEST_F(WriterFixture, empty)
{
    writer_->startRoot(Writer::CollectionType::Array);
    writer_->finish();
    checkOutputAndReset("[]");

    writer_->startRoot(Writer::CollectionType::Object);
    writer_->finish();
    checkOutputAndReset("{}");
}

TEST_F(WriterFixture, escaping)
{
    writer_->output("\\");
    checkOutputAndReset(R"("\\")");

    writer_->output("\"");
    checkOutputAndReset(R"("\"")");

    writer_->output("\\\"");
    checkOutputAndReset(R"("\\\"")");

    writer_->output("this contains a \\ in the middle of it.");
    checkOutputAndReset(R"("this contains a \\ in the middle of it.")");

    writer_->output("\b\f\n\r\t");
    checkOutputAndReset(R"("\b\f\n\r\t")");
}

TEST_F(WriterFixture, array)
{
    writer_->startRoot(Writer::CollectionType::Array);
    writer_->append(12);
    writer_->finish();
    checkOutputAndReset("[12]");
}

TEST_F(WriterFixture, long_array)
{
    writer_->startRoot(Writer::CollectionType::Array);
    writer_->append(12);
    writer_->append(true);
    writer_->append("hello");
    writer_->finish();
    checkOutputAndReset(R"([12,true,"hello"])");
}

TEST_F(WriterFixture, embedded_array_simple)
{
    writer_->startRoot(Writer::CollectionType::Array);
    writer_->startAppend(Writer::CollectionType::Array);
    writer_->finish();
    writer_->finish();
    checkOutputAndReset("[[]]");
}

TEST_F(WriterFixture, object)
{
    writer_->startRoot(Writer::CollectionType::Object);
    writer_->set("hello", "world");
    writer_->finish();
    checkOutputAndReset(R"({"hello":"world"})");
}

TEST_F(WriterFixture, complex_object)
{
    writer_->startRoot(Writer::CollectionType::Object);
    writer_->set("hello", "world");
    writer_->startSet(Writer::CollectionType::Array, "array");
    writer_->append(true);
    writer_->append(12);
    writer_->startAppend(Writer::CollectionType::Array);
    writer_->startAppend(Writer::CollectionType::Object);
    writer_->set("goodbye", "cruel world.");
    writer_->startSet(Writer::CollectionType::Array, "subarray");
    writer_->append(23.5);
    writer_->finishAll();
    checkOutputAndReset(
        R"({"hello":"world","array":[true,12,[{"goodbye":"cruel world.","subarray":[23.5]}]]})");
}

TEST_F(WriterFixture, json_value)
{
    json::Value value(json::ObjectValue);
    value["foo"] = 23;
    writer_->startRoot(Writer::CollectionType::Object);
    writer_->set("hello", value);
    writer_->finish();
    checkOutputAndReset(R"({"hello":{"foo":23}})");
}
