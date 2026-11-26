#pragma once

#include <xrpl/protocol/Rules.h>

#include <cstdint>

namespace xrpl {

/**
 * Add two uint32_t values with saturation at UINT32_MAX.
 *
 * @param rules  The current ledger rules used to check amendment status.
 * @param lhs    Left-hand operand.
 * @param rhs    Right-hand operand.
 * @return       @p lhs + @p rhs, saturated at UINT32_MAX when the amendment
 *               is active.
 */
uint32_t
saturatingAdd(Rules const& rules, uint32_t const lhs, uint32_t const rhs);

}  // namespace xrpl
