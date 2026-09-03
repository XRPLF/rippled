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
