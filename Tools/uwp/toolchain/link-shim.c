// WebKitWebView LINK.EXE shim (host-x64 binary, lives in llvm-tools/).
// ICU's mh-msys-msvc fragment links DLLs via LINK.EXE with gcc-style
// -Xlinker escapes that neither MSVC link nor raw lld-link accept.
// This strips -Xlinker, maps -lfoo to foo.lib, flips dash-form link flags
// to slash-form, and execs lld-link. Master: Tools/UWP/toolchain/link-shim.c
#include <windows.h>
#include <stdio.h>
#include <string.h>

// Host command-line capacities and process flags used by the shim.
enum {
    kCommandLineCapacity = 131072,
    kArgumentBufferCapacity = 8192,
    kFirstUserArgumentIndex = 1,
    kNoProcessCreationFlags = 0,
    kChildProcessCreateFailure = 1
};

// Toolchain path resolved by the x64 Visual Studio build environment.
static const char kLldLinkCommand[] = "\"C:\\PROGRA~2\\MICROS~3\\2022\\BUILDT~1\\VC\\Tools\\Llvm\\bin\\lld-link.exe\"";

// True when a dash-prefixed argument is a LINK option to rewrite as slash form.
static int is_link_word(const char* a)
{
    // LINK switches passed by ICU's generated make fragment, both cases.
    static const char* words[] = {
        "APPCONTAINER", "MACHINE:", "machine:", "SUBSYSTEM:", "subsystem:",
        "NODEFAULTLIB:", "nodefaultlib:", "LIBPATH:", "libpath:",
        "ENTRY:", "entry:", "INCREMENTAL", "incremental",
        "MANIFEST", "manifest", "DYNAMICBASE", "dynamicbase",
        "NXCOMPAT", "nxcompat", "DLL", "NOENTRY", "NOLOGO",
        "IMPLIB:", "OUT:", "DEF:", "OPT:", "IGNORE:",
        NULL
    };
    if (a[0] != '-') return FALSE;
    for (int i = 0; words[i]; i++) {
        size_t n = strlen(words[i]);
        if (!_strnicmp(a + 1, words[i], n)) return TRUE;
    }
    return FALSE;
}

// Translates GCC-style linker arguments and launches the bundled lld-link.
int main(int argc, char** argv)
{
    static char buf[kCommandLineCapacity];
    char* p = buf;
    p += sprintf(p, "%s", kLldLinkCommand);
    for (int i = kFirstUserArgumentIndex; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "-Xlinker")) continue;
        char tmp[kArgumentBufferCapacity];
        if (a[0] == '-' && a[1] == 'l' && a[2] != '\0') {
            sprintf(tmp, "%s.lib", a + 2);
            a = tmp;
            p += sprintf(p, " \"%s\"", a);
        } else if (is_link_word(a)) {
            p += sprintf(p, " /%s", a + 1);
        } else {
            p += sprintf(p, " \"%s\"", a);
        }
    }
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(NULL, buf, NULL, NULL, TRUE, kNoProcessCreationFlags, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "LINK.EXE shim: CreateProcess failed (%lu)\n", GetLastError());
        return kChildProcessCreateFailure;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = kChildProcessCreateFailure;
    GetExitCodeProcess(pi.hProcess, &code);
    return (int)code;
}
