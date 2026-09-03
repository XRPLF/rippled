//! A host call's arguments as they arrive: one type per declared parameter the
//! ABI marshals, built by `register.rs`'s generated closures and read by its
//! bodies.
//!
//! A wasm scalar (`i32`, `i64`) is passed through as itself and has no type here.
//! Everything else does, and what the types buy is that **a body cannot mistake
//! one argument for another**: an input region offered where an output one belongs
//! is a compile error naming both, where two loose `i32`s would have been a
//! rounding mode read as a buffer length. That is the half of the ABI a derived
//! signature cannot check, since every one of these is `i32, i32` on the wire.
//!
//! Reading is where each is judged, and every one of them can be refused —
//! `InU32`'s region must hold exactly four bytes, `InStr`'s must be UTF-8, and a
//! [`TraceCode`] must name a rendering. So the arguments arrive unchecked and are
//! checked in the order the body reads them, which is the order the guest is
//! answered in.

use crate::vm::MAX_FIELD_BYTES;
use core::ops::Range;
use xrpl_host_functions::{HostError, HostResult, TraceDataType};

/// A byte region as the guest declared it: the `(ptr, len)` pair off the wire, not
/// yet checked.
///
/// Private to this module, and the shared half of the four region types below:
/// they differ in what reading one means, not in what one is. The fields being out
/// of reach is what makes [`range`](Region::range) the only way to indices — the
/// check cannot be skipped, only deferred. Construction is infallible for that
/// reason: a call whose output region is malformed is then refused in the order
/// its own helper chooses, rather than at the moment the pair happened to be
/// formed.
#[derive(Copy, Clone)]
struct Region {
    ptr: i32,
    len: i32,
}

impl Region {
    fn new(ptr: i32, len: i32) -> Region {
        Region { ptr, len }
    }

    /// `start..end` as indices. The conversion is the negativity check — it fails on
    /// exactly the negative values — and the addition guards a 32-bit `usize`, where
    /// two `i32`s can sum past the end.
    fn range(self) -> HostResult<Range<usize>> {
        let (Ok(start), Ok(len)) = (usize::try_from(self.ptr), usize::try_from(self.len)) else {
            return Err(HostError::InvalidParams);
        };
        let end = start
            .checked_add(len)
            .ok_or(HostError::PointerOutOfBounds)?;
        Ok(start..end)
    }

    /// The region's bytes, refused past the field cap. No copy: the slice aliases
    /// `data`.
    fn read(self, data: &[u8]) -> HostResult<&[u8]> {
        let range = self.range()?;
        if range.len() > MAX_FIELD_BYTES {
            return Err(HostError::DataFieldTooLarge);
        }
        data.get(range).ok_or(HostError::PointerOutOfBounds)
    }
}

/// A declared `&[u8]`: an input region the host borrows.
#[derive(Copy, Clone)]
pub(crate) struct InBytes(Region);

impl InBytes {
    pub(crate) fn new(ptr: i32, len: i32) -> InBytes {
        InBytes(Region::new(ptr, len))
    }

    /// The region's bytes, aliasing the guest's memory rather than copied out of
    /// it.
    pub(crate) fn read(self, data: &[u8]) -> HostResult<&[u8]> {
        self.0.read(data)
    }
}

/// A declared `&str`: an input region whose bytes are text.
#[derive(Copy, Clone)]
pub(crate) struct InStr(Region);

impl InStr {
    pub(crate) fn new(ptr: i32, len: i32) -> InStr {
        InStr(Region::new(ptr, len))
    }

    /// The region's bytes as text. The read is also the UTF-8 check, so a host is
    /// never handed bytes claiming to be a `&str` and is not the one to validate
    /// them.
    pub(crate) fn read(self, data: &[u8]) -> HostResult<&str> {
        core::str::from_utf8(self.0.read(data)?).map_err(|_| HostError::InvalidParams)
    }
}

/// A declared `u32`: an input region holding the number as four little-endian
/// bytes, which is how the guest SDK passes a sequence number.
#[derive(Copy, Clone)]
pub(crate) struct InU32(Region);

impl InU32 {
    pub(crate) fn new(ptr: i32, len: i32) -> InU32 {
        InU32(Region::new(ptr, len))
    }

    /// The number the region holds. Exactly four bytes, since the width is the
    /// ABI's rather than the guest's; `InvalidParams` for any other length.
    pub(crate) fn read(self, data: &[u8]) -> HostResult<u32> {
        let bytes: [u8; 4] = self
            .0
            .read(data)?
            .try_into()
            .map_err(|_| HostError::InvalidParams)?;
        Ok(u32::from_le_bytes(bytes))
    }
}

/// A declared `&mut [u8]`: the region the host's answer is written to.
///
/// It has no `read`: what a call may put here is decided by the helper in
/// `abi.rs` that serves it, against the value's length and the run's budget, and
/// the host is never handed the guest's capacity.
#[derive(Copy, Clone)]
pub(crate) struct OutBytes(Region);

impl OutBytes {
    pub(crate) fn new(ptr: i32, len: i32) -> OutBytes {
        OutBytes(Region::new(ptr, len))
    }

    pub(crate) fn range(self) -> HostResult<Range<usize>> {
        self.0.range()
    }
}

/// A declared `TraceDataType`: the `i32` code naming how `trace` is to render its
/// data.
///
/// The one marshalled argument that is not a region — it is a single wasm scalar,
/// and reading it needs no guest memory.
#[derive(Copy, Clone)]
pub(crate) struct TraceCode(i32);

impl TraceCode {
    pub(crate) fn new(code: i32) -> TraceCode {
        TraceCode(code)
    }

    /// The type the code names; `InvalidParams` if it names none, since a
    /// rendering the guest did not ask for is not one to guess at.
    pub(crate) fn read(self) -> HostResult<TraceDataType> {
        TraceDataType::from_code(self.0).ok_or(HostError::InvalidParams)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The guest memory these tests read out of: sixteen bytes at index 0.
    const MEMORY: [u8; 16] = [
        0x78, 0x56, 0x34, 0x12, b'h', b'i', 0xff, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    ];

    #[test]
    fn a_u32_argument_is_four_little_endian_bytes() {
        assert_eq!(InU32::new(0, 4).read(&MEMORY), Ok(0x1234_5678));
    }

    /// The width is the ABI's, so a region of any other length names no number —
    /// including a longer one, whose first four bytes would otherwise be read as
    /// the answer.
    #[test]
    fn a_u32_argument_of_any_other_width_is_refused() {
        for len in [0, 1, 3, 5, 8] {
            assert_eq!(
                InU32::new(0, len).read(&MEMORY),
                Err(HostError::InvalidParams),
                "{len} bytes"
            );
        }
    }

    /// The read is the UTF-8 check: a host implementing `trace` is handed a `&str`
    /// and has nothing left to validate.
    #[test]
    fn a_str_argument_is_checked_where_it_is_read() {
        assert_eq!(InStr::new(4, 2).read(&MEMORY), Ok("hi"));
        assert_eq!(
            InStr::new(6, 1).read(&MEMORY),
            Err(HostError::InvalidParams),
            "0xff is not UTF-8"
        );
    }

    /// A region past the end of guest memory is refused rather than clamped, and
    /// one past the field cap is refused before the memory is consulted at all.
    #[test]
    fn a_region_is_held_to_the_memory_and_to_the_field_cap() {
        assert_eq!(
            InBytes::new(8, 16).read(&MEMORY),
            Err(HostError::PointerOutOfBounds)
        );

        let past_the_cap = i32::try_from(MAX_FIELD_BYTES).expect("the cap is a small constant") + 1;
        assert_eq!(
            InBytes::new(0, past_the_cap).read(&MEMORY),
            Err(HostError::DataFieldTooLarge)
        );

        assert_eq!(
            InBytes::new(-1, 4).read(&MEMORY),
            Err(HostError::InvalidParams)
        );
    }

    /// Every code the ABI has, and nothing else. The engine drops a call it cannot
    /// read rather than rendering the data some other way.
    #[test]
    fn a_trace_code_names_a_rendering_or_none() {
        for &data_type in TraceDataType::ALL {
            assert_eq!(TraceCode::new(data_type.code()).read(), Ok(data_type));
        }

        for code in [0, -1, i32::MAX] {
            assert_eq!(
                TraceCode::new(code).read(),
                Err(HostError::InvalidParams),
                "{code}"
            );
        }
    }
}
