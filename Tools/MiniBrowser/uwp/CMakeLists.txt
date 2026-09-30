# STUB: MiniBrowser UWP — NOT added to any build yet.
# Wired only after the WK_WEBKITVIEW engine configures for ARM32-UWP.
# Based on ../win/CMakeLists.txt with desktop-only pieces removed:
#  - no user32/comctl32/shlwapi (banned in AppContainer)
#  - no WIN32_EXECUTABLE / WinMain (UWP App entry instead)
#  - no InjectedBundle (no child process in UWP, single process only)

set(MiniBrowserUWP_SOURCES
    README.md
)

# set(MiniBrowserUWP_PRIVATE_DEFINITIONS WK_WEBKITVIEW=1)
# set(MiniBrowserUWP_FRAMEWORKS JavaScriptCore PAL WTF WebCore WebKit)
# WEBKIT_EXECUTABLE_DECLARE(MiniBrowserUWP)
# WEBKIT_EXECUTABLE(MiniBrowserUWP)
