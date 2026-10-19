//! Exercises what `#[coded_enum]` generates for [`HostError`] and [`HostTrapError`]:
//! the wire codes, the sets their `ALL` names, and the round trip between them.
//!
//! The codes are consensus input — they are what a guest reads off a failed host
//! call — so they are pinned here as literals and derived everywhere else.

use xrpl_host_functions::{HostError, HostTrapError};

/// The whole set, written out in the order `ALL` gives it: the one place the wire
/// codes appear as literals, and a deliberate change-detector, since a code that
/// moves changes what every deployed guest is told.
#[test]
fn the_error_table_matches_the_declarations() {
    let table: Vec<(HostError, i32)> = HostError::ALL
        .iter()
        .map(|&error| (error, error.code()))
        .collect();

    assert_eq!(
        table,
        [
            (HostError::FieldNotFound, -1),
            (HostError::BufferTooSmall, -2),
            (HostError::NoArray, -3),
            (HostError::NotLeafField, -4),
            (HostError::LocatorMalformed, -5),
            (HostError::SlotOutRange, -6),
            (HostError::SlotsFull, -7),
            (HostError::EmptySlot, -8),
            (HostError::LedgerObjNotFound, -9),
            (HostError::OutOfTransferLimit, -10),
            (HostError::DataFieldTooLarge, -11),
            (HostError::PointerOutOfBounds, -12),
            (HostError::InvalidParams, -13),
            (HostError::InvalidAccount, -14),
            (HostError::InvalidField, -15),
            (HostError::IndexOutOfBounds, -16),
            (HostError::FloatInputMalformed, -17),
            (HostError::FloatComputationError, -18),
        ]
    );
}

/// The trap codes never reach a guest, but they cross from xrpld as wire values, so
/// they are pinned as well.
#[test]
fn the_trap_table_matches_the_declarations() {
    let table: Vec<(HostTrapError, i32)> = HostTrapError::ALL
        .iter()
        .map(|&error| (error, error.code()))
        .collect();

    assert_eq!(table, [(HostTrapError::InternalFatal, i32::MIN)]);
}

/// The guest-facing set is contiguous from `-1`, so the next code appended is
/// `-(len + 1)`.
#[test]
fn the_guest_codes_are_contiguous_from_minus_one() {
    for (i, &error) in HostError::ALL.iter().enumerate() {
        let expected = -i32::try_from(i + 1).expect("the set is far smaller than i32");
        assert_eq!(error.code(), expected, "{error:?}");
    }
}

/// A trap code read as a guest code, or the reverse, would put an error on the wrong
/// channel: `xrpl-wasm-vm-ffi` decodes a code by trying both sets.
#[test]
fn the_guest_and_trap_codes_do_not_overlap() {
    for &trap in HostTrapError::ALL {
        assert_eq!(HostError::from_code(trap.code()), None, "{trap:?}");
    }
    for &error in HostError::ALL {
        assert_eq!(HostTrapError::from_code(error.code()), None, "{error:?}");
    }
}

/// Every code comes back as the error that produced it, so a caller reading a
/// negative return value recovers the condition and not a neighbouring one. The
/// tables above pin the numbers; this adds only the round trip.
#[test]
fn every_wire_code_round_trips_back_to_its_error() {
    for &error in HostError::ALL {
        assert_eq!(HostError::from_code(error.code()), Some(error), "{error:?}");
    }
    for &trap in HostTrapError::ALL {
        assert_eq!(
            HostTrapError::from_code(trap.code()),
            Some(trap),
            "{trap:?}"
        );
    }
}

/// A code from outside both sets names no error and is not rounded to a neighbouring
/// one; what it means instead is the crossing's to decide, in `xrpl-wasm-vm-ffi`'s
/// `host_error`.
///
/// `-19` is the code that would be appended next; `i32::MIN + 1` is next to the trap
/// code and unassigned, which is what makes it a value rather than a range.
#[test]
fn a_code_outside_the_sets_names_no_error() {
    for code in [-19, i32::MIN + 1, 0, 1, i32::MAX] {
        assert_eq!(HostError::from_code(code), None, "{code}");
        assert_eq!(HostTrapError::from_code(code), None, "{code}");
    }
}
