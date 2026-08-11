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
        ]
    );
}

/// The set is `-1 ..= -20` and nothing else: this enum is xrpld's `HostFunctionError`
/// and every entry is a code some contract may read, so a condition with no number to
/// answer with is not one of these — it is a `Fault` in the engine.
#[test]
fn every_code_is_in_the_shared_range() {
    let outside: Vec<HostError> = HostError::ALL
        .iter()
        .copied()
        .filter(|error| !(-20..=-1).contains(&error.code()))
        .collect();

    assert!(outside.is_empty(), "outside -1..=-20: {outside:?}");
    assert_eq!(HostError::ALL.len(), 20);
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

/// A code from outside the set is `Unimplemented`: a host answering something this
/// ABI does not define has not served the call, whatever it meant by it, and success
/// is not an error at all.
#[test]
fn a_code_outside_the_set_is_unimplemented() {
    let unassigned = -(HostError::ALL.len() as i32) - 1;

    for code in [unassigned, i32::MIN, 0, 1, i32::MAX] {
        assert_eq!(
            HostError::from_code(code),
            HostError::Unimplemented,
            "{code}"
        );
    }
}
