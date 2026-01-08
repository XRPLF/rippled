#ifndef XRPL_PROTOCOL_LEDGERSHORTCUT_H_INCLUDED
#define XRPL_PROTOCOL_LEDGERSHORTCUT_H_INCLUDED

namespace xrpl {

/**
 * @brief Enumeration of ledger shortcuts for specifying which ledger to use.
 *
 * These shortcuts provide a convenient way to reference commonly used ledgers
 * without needing to specify their exact hash or sequence number.
 */
enum class LedgerShortcut {
    /** The current working ledger (open, not yet closed) */
    Current,

    /** The most recently closed ledger (may not be validated) */
    Closed,

    /** The most recently validated ledger */
    Validated
};

}  // namespace xrpl

#endif
