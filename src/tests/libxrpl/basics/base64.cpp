#include <xrpl/basics/base64.h>

#include <doctest/doctest.h>

#include <string>

using namespace ripple;

static void
check(std::string const& in, std::string const& out)
{
    auto const encoded = base64_encode(in);
    CHECK(encoded == out);
    CHECK(base64_decode(encoded) == in);
}

TEST_CASE("base64")
{
    check("", "");
    check("f", "Zg==");
    check("fo", "Zm8=");
    check("foo", "Zm9v");
    check("foob", "Zm9vYg==");
    check("fooba", "Zm9vYmE=");
    check("foobar", "Zm9vYmFy");

    check(
        "Man is distinguished, not only by his reason, but by this "
        "singular passion from "
        "other animals, which is a lust of the mind, that by a "
        "perseverance of delight "
        "in the continued and indefatigable generation of knowledge, "
        "exceeds the short "
        "vehemence of any carnal pleasure.",
        "TWFuIGlzIGRpc3Rpbmd1aXNoZWQsIG5vdCBvbmx5IGJ5IGhpcyByZWFzb24sIGJ1dC"
        "BieSB0aGlz"
        "IHNpbmd1bGFyIHBhc3Npb24gZnJvbSBvdGhlciBhbmltYWxzLCB3aGljaCBpcyBhIG"
        "x1c3Qgb2Yg"
        "dGhlIG1pbmQsIHRoYXQgYnkgYSBwZXJzZXZlcmFuY2Ugb2YgZGVsaWdodCBpbiB0aG"
        "UgY29udGlu"
        "dWVkIGFuZCBpbmRlZmF0aWdhYmxlIGdlbmVyYXRpb24gb2Yga25vd2xlZGdlLCBleG"
        "NlZWRzIHRo"
        "ZSBzaG9ydCB2ZWhlbWVuY2Ugb2YgYW55IGNhcm5hbCBwbGVhc3VyZS4=");

    std::string const notBase64 = "not_base64!!";
    std::string const truncated = "not";
    CHECK(base64_decode(notBase64) == base64_decode(truncated));
}
