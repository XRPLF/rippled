#pragma once

#include <xrpl/tx/wasm/WasmCommon.h>

#include <cstdint>
#include <initializer_list>

namespace xrpl::test {

// Owns the wire bytes a `FieldLocator` views, so it must outlive every locator taken from
// it. As a temporary in the call expression, it does.
class OwnedLocator
{
    Bytes bytes_;

public:
    OwnedLocator(std::initializer_list<std::int32_t> steps);

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator FieldLocator() const;
};

OwnedLocator
locator(std::initializer_list<std::int32_t> steps);

}  // namespace xrpl::test
