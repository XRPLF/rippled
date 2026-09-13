#include <xrpl/protocol/StructuredData.h>

#include <xrpl/protocol/Protocol.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace xrpl {

namespace {

struct Cursor
{
    Slice s;
    std::size_t i = 0;

    [[nodiscard]] std::size_t
    remaining() const
    {
        return s.size() - i;
    }
};

// Width of a fixed-size type; nullopt for str/bin/array/tuple codes and
// for reserved codes.
std::optional<std::size_t>
fixedWidth(std::uint8_t code)
{
    switch (static_cast<SchemaType>(code))
    {
        case SchemaType::boolean:
        case SchemaType::u8:
            return 1;
        case SchemaType::u16:
            return 2;
        case SchemaType::u32:
            return 4;
        case SchemaType::u64:
        case SchemaType::xfl:
            return 8;
        case SchemaType::u128:
            return 16;
        case SchemaType::account:
        case SchemaType::currency:
        case SchemaType::h160:
            return 20;
        case SchemaType::u256:
        case SchemaType::h256:
            return 32;
        case SchemaType::pubkey:
            return 33;
        default:
            return std::nullopt;
    }
}

// Standard XRPL VL length prefix. Returns the payload length and
// advances the cursor past the prefix, or nullopt if the prefix is
// truncated or invalid.
std::optional<std::size_t>
readVLPrefix(Cursor& dc)
{
    if (dc.remaining() < 1)
        return std::nullopt;
    std::size_t const b0 = dc.s[dc.i++];
    if (b0 <= 192)
        return b0;
    if (b0 <= 240)
    {
        if (dc.remaining() < 1)
            return std::nullopt;
        std::size_t const b1 = dc.s[dc.i++];
        return 193 + (b0 - 193) * 256 + b1;
    }
    if (b0 <= 254)
    {
        if (dc.remaining() < 2)
            return std::nullopt;
        std::size_t const b1 = dc.s[dc.i++];
        std::size_t const b2 = dc.s[dc.i++];
        return 12481 + (b0 - 241) * 65536 + b1 * 256 + b2;
    }
    return std::nullopt;
}

// Walk one type starting at sc.i, advancing sc past it. When dc is
// non-null, consume the matching data. Re-invoked per array element
// with a reset schema cursor so the element type is applied count
// times against a single data cursor.
bool
parseType(Cursor& sc, Cursor* dc, std::size_t depth)
{
    if (sc.remaining() < 1)
        return false;
    std::uint8_t const code = sc.s[sc.i++];

    if (auto const width = fixedWidth(code))
    {
        if (dc)
        {
            if (dc->remaining() < *width)
                return false;
            if (static_cast<SchemaType>(code) == SchemaType::boolean && dc->s[dc->i] > 1)
                return false;
            dc->i += *width;
        }
        return true;
    }

    switch (static_cast<SchemaType>(code))
    {
        case SchemaType::str:
        case SchemaType::bin: {
            if (dc)
            {
                auto const len = readVLPrefix(*dc);
                if (!len || dc->remaining() < *len)
                    return false;
                dc->i += *len;
            }
            return true;
        }

        case SchemaType::array: {
            if (depth + 1 > kMaxSchemaDepth)
                return false;
            // Validate the element type once (schema-only) and record
            // its extent, then replay it per element against the data.
            std::size_t const elemBegin = sc.i;
            if (!parseType(sc, nullptr, depth + 1))
                return false;
            if (dc)
            {
                if (dc->remaining() < 1)
                    return false;
                std::size_t const count = dc->s[dc->i++];
                for (std::size_t n = 0; n < count; ++n)
                {
                    Cursor esc{sc.s, elemBegin};
                    if (!parseType(esc, dc, depth + 1))
                        return false;
                }
            }
            return true;
        }

        case SchemaType::tupleOpen: {
            if (depth + 1 > kMaxSchemaDepth)
                return false;
            while (true)
            {
                if (sc.remaining() < 1)
                    return false;  // unterminated tuple
                if (static_cast<SchemaType>(sc.s[sc.i]) == SchemaType::tupleClose)
                {
                    ++sc.i;
                    return true;
                }
                if (!parseType(sc, dc, depth + 1))
                    return false;
            }
        }

        default:
            // Reserved codes and a tupleClose without a matching open.
            return false;
    }
}

}  // namespace

bool
isWellFormedSchema(Slice schema)
{
    if (schema.empty() || schema.size() > kMaxSchemaLength)
        return false;
    Cursor sc{schema};
    while (sc.i < schema.size())
    {
        if (!parseType(sc, nullptr, 0))
            return false;
    }
    return true;
}

bool
dataMatchesSchema(Slice schema, Slice data)
{
    if (schema.empty() || schema.size() > kMaxSchemaLength)
        return false;
    Cursor sc{schema};
    Cursor dc{data};
    while (sc.i < schema.size())
    {
        if (!parseType(sc, &dc, 0))
            return false;
    }
    return dc.i == data.size();
}

}  // namespace xrpl
