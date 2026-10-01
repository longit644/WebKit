list(APPEND WTF_SOURCES
    generic/WorkQueueGeneric.cpp

    text/win/StringWin.cpp
    text/win/TextBreakIteratorInternalICUWin.cpp

    win/CPUTimeWin.cpp
    win/DbgHelperWin.cpp
    win/FileHandleWin.cpp
    win/FileSystemWin.cpp
    win/LanguageWin.cpp
    win/LoggingWin.cpp
    win/MainThreadWin.cpp
    win/MappedFileDataWin.cpp
    win/MemoryFootprintWin.cpp
    win/MemoryPressureHandlerWin.cpp
    win/OSAllocatorWin.cpp
    win/PathWalker.cpp
    win/SignalsWin.cpp
    win/ThreadingWin.cpp
    win/WTFCRTDebug.cpp
    win/Win32Handle.cpp
)

list(APPEND WTF_PUBLIC_HEADERS
    PlatformEnableWin.h

    text/win/WCharStringExtras.h

    win/DbgHelperWin.h
    win/GDIObject.h
    win/PathWalker.h
    win/SoftLinking.h
    win/WTFCRTDebug.h
    win/Win32Handle.h
)

list(APPEND WTF_LIBRARIES
    DbgHelp
    shlwapi
    synchronization
    winmm
)

if (WK_UWP)
    # WebKitWebView: RunLoopWin needs an HWND, impossible in an AppContainer.
    list(APPEND WTF_SOURCES generic/RunLoopGeneric.cpp)
    # WebKitWebView: DbgHelp/shlwapi/winmm are desktop-only (no ARM import libs,
    # no store LoadLibrary); DbgHelp paths stubbed, timers skipped.
    # synchronization (SynchAPI) is App-partition legal.
    list(REMOVE_ITEM WTF_LIBRARIES DbgHelp shlwapi winmm)
else ()
    list(APPEND WTF_SOURCES win/RunLoopWin.cpp)
endif ()
