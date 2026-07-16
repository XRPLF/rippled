#pragma once

// The CollectorManager interface moved to <xrpl/core/CollectorManager.h> so it
// is visible to libxrpl (which cannot depend on xrpld). This shim preserves the
// historical include path for existing xrpld consumers.
#include <xrpl/core/CollectorManager.h>
