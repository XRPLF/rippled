#include <xrpl/json/Writer.h>

#include <google/protobuf/stubs/port.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace xrpl;
using namespace Json;

class WriterFixture : public ::testing::Test
{
protected:
    std::string output;
    std::unique_ptr<Writer> writer;

    void
    SetUp() override
    {
        writer = std::make_unique<Writer>(stringOutput(output));
    }

    void
    reset()
    {
        output.clear();
        writer = std::make_unique<Writer>(stringOutput(output));
    }

    void
    expectOutput(std::string const& expected) const
    {
        EXPECT_EQ(output, expected);
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
    EXPECT_TRUE(output.empty());
    checkOutputAndReset("");
}

TEST_F(WriterFixture, near_trivial)
{
    EXPECT_TRUE(output.empty());
    writer->output(0);
    checkOutputAndReset("0");
}

TEST_F(WriterFixture, primitives)
{
    writer->output(true);
    checkOutputAndReset("true");

    writer->output(false);
    checkOutputAndReset("false");

    writer->output(23);
    checkOutputAndReset("23");

    writer->output(23.0);
    checkOutputAndReset("23.0");

    writer->output(23.5);
    checkOutputAndReset("23.5");

    writer->output("a string");
    checkOutputAndReset(R"("a string")");

    writer->output(nullptr);
    checkOutputAndReset("null");
}

TEST_F(WriterFixture, empty)
{
    writer->startRoot(Writer::array);
    writer->finish();
    checkOutputAndReset("[]");

    writer->startRoot(Writer::object);
    writer->finish();
    checkOutputAndReset("{}");
}

TEST_F(WriterFixture, escaping)
{
    writer->output("\\");
    checkOutputAndReset(R"("\\")");

    writer->output("\"");
    checkOutputAndReset(R"("\"")");

    writer->output("\\\"");
    checkOutputAndReset(R"("\\\"")");

    writer->output("this contains a \\ in the middle of it.");
    checkOutputAndReset(R"("this contains a \\ in the middle of it.")");

    writer->output("\b\f\n\r\t");
    checkOutputAndReset(R"("\b\f\n\r\t")");
}

TEST_F(WriterFixture, array)
{
    writer->startRoot(Writer::array);
    writer->append(12);
    writer->finish();
    checkOutputAndReset("[12]");
}

TEST_F(WriterFixture, long_array)
{
    writer->startRoot(Writer::array);
    writer->append(12);
    writer->append(true);
    writer->append("hello");
    writer->finish();
    checkOutputAndReset(R"([12,true,"hello"])");
}

TEST_F(WriterFixture, embedded_array_simple)
{
    writer->startRoot(Writer::array);
    writer->startAppend(Writer::array);
    writer->finish();
    writer->finish();
    checkOutputAndReset("[[]]");
}

TEST_F(WriterFixture, object)
{
    writer->startRoot(Writer::object);
    writer->set("hello", "world");
    writer->finish();
    checkOutputAndReset(R"({"hello":"world"})");
}

TEST_F(WriterFixture, complex_object)
{
    writer->startRoot(Writer::object);
    writer->set("hello", "world");
    writer->startSet(Writer::array, "array");
    writer->append(true);
    writer->append(12);
    writer->startAppend(Writer::array);
    writer->startAppend(Writer::object);
    writer->set("goodbye", "cruel world.");
    writer->startSet(Writer::array, "subarray");
    writer->append(23.5);
    writer->finishAll();
    checkOutputAndReset(
        R"({"hello":"world","array":[true,12,[{"goodbye":"cruel world.","subarray":[23.5]}]]})");
}

TEST_F(WriterFixture, json_value)
{
    Json::Value value(Json::objectValue);
    value["foo"] = 23;
    writer->startRoot(Writer::object);
    writer->set("hello", value);
    writer->finish();
    checkOutputAndReset(R"({"hello":{"foo":23}})");
}
