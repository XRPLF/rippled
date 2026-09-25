#pragma once

namespace xrpl {

class Section;

/**
 * Validate configured amendment votes without accessing the wallet database.
 */
void
validateAmendmentConfig(Section const& enabled, Section const& vetoed);

}  // namespace xrpl
