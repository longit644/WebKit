if (NOT WK_WEBKITVIEW_UWP)
    # WebKitView: no BSTR in the AppContainer API partition.
    list(APPEND JavaScriptCore_SOURCES
        API/JSStringRefBSTR.cpp
    )

    list(APPEND JavaScriptCore_PUBLIC_FRAMEWORK_HEADERS
        API/JSStringRefBSTR.h
    )
endif ()

list(APPEND JavaScriptCore_PUBLIC_FRAMEWORK_HEADERS
    API/JavaScriptCore.h
)

if (ENABLE_REMOTE_INSPECTOR)
    include(inspector/remote/Socket.cmake)
else ()
    list(REMOVE_ITEM JavaScriptCore_SOURCES
        inspector/JSGlobalObjectInspectorController.cpp
    )
endif ()
