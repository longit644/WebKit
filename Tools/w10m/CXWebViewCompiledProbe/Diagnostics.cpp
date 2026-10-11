#include "Diagnostics.h"
#include "pch.h"

#include <cstdio>
#include <cstring>

namespace WebKitCompiledProbe {

// Write this run's package-local progress/final report and flush before closing its handle.
void SaveContractReport(const char* text)
{
    auto path = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\webkit-compiled-result.txt";
    auto file = CreateFile2(path->Data(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}

// Save the originating WinRT error with version identity and a failed aggregate.
void SaveContractFailure(long result, Platform::String^ message)
{
    char detail[1024] { };
    if (message) WideCharToMultiByte(CP_UTF8, 0, message->Data(), -1, detail, sizeof(detail), nullptr, nullptr);
    char report[1400];
    sprintf_s(report, "version=" COMPILED_PROBE_VERSION "\r\nresult=FAIL\r\nHRESULT=0x%08lX\r\nmessage=%s\r\n", static_cast<unsigned long>(result), detail);
    SaveContractReport(report);
}

} // namespace WebKitCompiledProbe
