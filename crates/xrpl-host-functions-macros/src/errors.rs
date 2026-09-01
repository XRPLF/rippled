/// Folds accumulated diagnostics into the single error a macro can return.
///
/// `syn::Error` is itself a collection: `combine` appends, and
/// `into_compile_error` emits one `compile_error!` per recorded span. Folding
/// instead of returning the first error means every mistake in a
/// `host_functions!` block surfaces in one build rather than one per rebuild.
pub(crate) fn combine(errors: Vec<syn::Error>) -> Option<syn::Error> {
    errors.into_iter().reduce(|mut first, next| {
        first.combine(next);
        first
    })
}

/// `value`, unless diagnostics were recorded: the folded error then, as the one
/// `Err` a check answers with.
pub(crate) fn into_result<T>(value: T, errors: Vec<syn::Error>) -> syn::Result<T> {
    match combine(errors) {
        Some(error) => Err(error),
        None => Ok(value),
    }
}

/// Files `result`'s diagnostics and answers `None`, or answers its value.
///
/// The one place a `syn::Result` joins an accumulator, so a check that yields a
/// value reports the same way as one that yields nothing — which is what lets
/// the caller keep going and report the rest of the declaration's mistakes.
pub(crate) fn record<T>(result: syn::Result<T>, errors: &mut Vec<syn::Error>) -> Option<T> {
    match result {
        Ok(value) => Some(value),
        Err(error) => {
            errors.push(error);
            None
        }
    }
}
