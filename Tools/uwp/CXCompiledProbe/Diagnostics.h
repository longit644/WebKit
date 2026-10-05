#pragma once

namespace CXCompileProbe {

// Writes a complete, flushed result so event-loop lifetime does not hide failures.
void RecordResult(const char* text);

// Preserves the WinRT failure code and message instead of inferring from a splash screen.
void RecordFailure(long result, Platform::String ^ message);

}
