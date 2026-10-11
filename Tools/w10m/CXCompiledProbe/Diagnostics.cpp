#include "Diagnostics.h"
#include "pch.h"
#include <cstdio>
#include <cstring>

namespace CXCompileProbe {

// Use the package's LocalState directory; no device-specific paths are authored.
void RecordResult(const char* text)
{
    auto path = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\compiled-xaml-result.txt";
    HANDLE file = CreateFile2(path->Data(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return;
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(strlen(text)), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
}

// The summary is ASCII; convert the supplied WinRT error message explicitly to UTF-8.
void RecordFailure(long result, Platform::String ^ message)
{
    char value[2048] = {};
    if (message)
        WideCharToMultiByte(CP_UTF8, 0, message->Data(), -1, value, sizeof(value), nullptr, nullptr);
    char text[2304];
    sprintf_s(text, "version=1.0.0.6\r\nresult=FAIL\r\nhr=0x%08lX\r\nmessage=%s\r\n", static_cast<unsigned long>(result), value);
    RecordResult(text);
}

}
