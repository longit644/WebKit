#pragma once

namespace WebKitCompiledProbe {

// Save one version-tagged ASCII progress/final report to this package's LocalState.
void SaveContractReport(const char*);
// Preserve actual construction/check failures rather than accepting missing assertions.
void SaveContractFailure(long result, Platform::String^ message);

} // namespace WebKitCompiledProbe
