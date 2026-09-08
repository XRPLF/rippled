use crate::vm::MAX_FIELD_BYTES;
use core::ops::Range;
use xrpl_host_functions::{HostError, HostResult};

/// A byte region as the guest declared it: the `(ptr, len)` pair off the wire, not
/// yet checked.
///
/// Every byte parameter in this ABI is such a pair, so pairing them once at the wire
/// boundary is what keeps the helpers in `abi.rs` from each taking two loose integers
/// they could be handed in either order.
///
/// It lives in a module of its own so that the fields are out of reach and
/// [`range`](Region::range) is the *only* way to indices — the check cannot be
/// skipped, only deferred. Construction is infallible for that reason: a call whose
/// output region is malformed is then refused in the order its own helper chooses,
/// rather than at the moment the pair happened to be formed.
#[derive(Copy, Clone)]
pub(crate) struct Region {
    ptr: i32,
    len: i32,
}

impl Region {
    pub(crate) fn new(ptr: i32, len: i32) -> Region {
        Region { ptr, len }
    }

    /// `start..end` as indices. The conversion is the negativity check — it fails on
    /// exactly the negative values — and the addition guards a 32-bit `usize`, where
    /// two `i32`s can sum past the end.
    pub(crate) fn range(self) -> HostResult<Range<usize>> {
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
    pub(crate) fn read(self, data: &[u8]) -> HostResult<&[u8]> {
        let range = self.range()?;
        if range.len() > MAX_FIELD_BYTES {
            return Err(HostError::DataFieldTooLarge);
        }
        data.get(range).ok_or(HostError::PointerOutOfBounds)
    }
}
