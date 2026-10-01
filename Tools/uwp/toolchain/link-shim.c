// WebKitWebView LINK.EXE shim (host-x64 binary, lives in llvm-tools/).
// ICU's mh-msys-msvc fragment links DLLs via LINK.EXE with gcc-style
// -Xlinker escapes that neither MSVC link nor raw lld-link accept.
// This strips -Xlinker, maps -lfoo to foo.lib, flips dash-form link flags
// to slash-form, and execs lld-link. Master: Tools/UWP/toolchain/link-shim.c
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int is_link_word(const char* a)
{
    static const char* words[] = {
        "APPCONTAINER", "MACHINE:", "machine:", "SUBSYSTEM:", "subsystem:",
        "NODEFAULTLIB:", "nodefaultlib:", "LIBPATH:", "libpath:",
        "ENTRY:", "entry:", "INCREMENTAL", "incremental",
        "MANIFEST", "manifest", "DYNAMICBASE", "dynamicbase",
        "NXCOMPAT", "nxcompat", "DLL", "NOENTRY", "NOLOGO",
        "IMPLIB:", "OUT:", "DEF:", "OPT:", "IGNORE:",
        NULL
    };
    if (a[0] != '-') return 0;
    for (int i = 0; words[i]; i++) {
        size_t n = strlen(words[i]);
        if (_strnicmp(a + 1, words[i], n) == 0) return 1;
    }
    return 0;
}

int main(int argc, char** argv)
{
    static char buf[131072];
    char* p = buf;
    p += sprintf(p, "\"C:\\PROGRA~2\\MICROS~3\\2022\\BUILDT~1\\VC\\Tools\\Llvm\\bin\\lld-link.exe\"");
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (strcmp(a, "-Xlinker") == 0) continue;
        char tmp[8192];
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
    if (!CreateProcessA(NULL, buf, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "LINK.EXE shim: CreateProcess failed (%lu)\n", GetLastError());
        return 1;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    return (int)code;
}
