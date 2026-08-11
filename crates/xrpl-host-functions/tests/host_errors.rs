//! Exercises what `host_errors!` generates: the wire codes, the set
//! [`HostError::ALL`] names, and the round trip between them.
//!
//! The codes are consensus input — they are what a guest reads off a failed host
//! call — so they are pinned here as literals and derived everywhere else.

use xrpl_host_functions::HostError;

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
            (HostError::Unimplemented, -1),
            (HostError::FieldNotFound, -2),
            (HostError::BufferTooSmall, -3),
            (HostError::NoArray, -4),
            (HostError::NotLeafField, -5),
            (HostError::LocatorMalformed, -6),
            (HostError::SlotOutRange, -7),
            (HostError::SlotsFull, -8),
            (HostError::EmptySlot, -9),
            (HostError::LedgerObjNotFound, -10),
            (HostError::OutOfTransferLimit, -11),
            (HostError::DataFieldTooLarge, -12),
            (HostError::PointerOutOfBounds, -13),
            (HostError::NoMemExported, -14),
            (HostError::InvalidParams, -15),
            (HostError::InvalidAccount, -16),
            (HostError::InvalidField, -17),
            (HostError::IndexOutOfBounds, -18),
            (HostError::FloatInputMalformed, -19),
            (HostError::FloatComputationError, -20),
            (HostError::InternalFatal, i32::MIN),
        ]
    );
}

/// The guest-facing set is `-1 ..= -20` and nothing else: those entries are xrpld's
/// `HostFunctionError`, and each is a code some contract may read.
///
/// `InternalFatal` is the one deliberate exception, exempted by name rather than by
/// widening the range: a condition with no number a contract can act on needs no number
/// in the range a contract reads, and holding it at `i32::MIN` is what keeps it from
/// ever colliding with a code appended to xrpld's list.
#[test]
fn every_code_but_the_sentinel_is_in_the_shared_range() {
    let shared: Vec<HostError> = HostError::ALL
        .iter()
        .copied()
        .filter(|&error| error != HostError::InternalFatal)
        .collect();

    let outside: Vec<HostError> = shared
        .iter()
        .copied()
        .filter(|error| !(-20..=-1).contains(&error.code()))
        .collect();

    assert!(outside.is_empty(), "outside -1..=-20: {outside:?}");
    assert_eq!(shared.len(), 20);
    assert_eq!(HostError::InternalFatal.code(), i32::MIN);
    assert_eq!(HostError::ALL.len(), 21);
}

/// Every code a guest can be handed comes back as the error that produced it, so a
/// caller reading a negative return value recovers the condition and not a
/// neighbouring one. The table above pins the numbers; this adds only the round
/// trip.
#[test]
fn every_wire_code_round_trips_back_to_its_error() {
    for &error in HostError::ALL {
        assert_eq!(HostError::from_code(error.code()), error, "{error:?}");
    }
}

/// A code from outside the set is `InternalFatal`: a host answering something this ABI
/// does not define has not served the call, whatever it meant by it, and success is not
/// an error at all.
///
/// `-21` is the code xrpld would append next, so it is the one that decides whether a
/// list this crate has not caught up with reaches a guest or stops the run. `i32::MIN +
/// 1` is next to the sentinel and unassigned, which is what makes the sentinel a value
/// rather than a range.
#[test]
fn a_code_outside_the_set_is_internal_fatal() {
    for code in [-21, i32::MIN + 1, 0, 1, i32::MAX] {
        assert_eq!(
            HostError::from_code(code),
            HostError::InternalFatal,
            "{code}"
        );
    }
}
