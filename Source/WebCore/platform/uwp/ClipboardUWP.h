// WinRT clipboard transport for WebKitWebViewUWP.
#pragma once

#include "PlatformExportMacros.h"
#include <windows.h>
#include <wtf/CompletionHandler.h>
#include <wtf/Function.h>
#include <wtf/text/WTFString.h>

namespace WebCore::ClipboardUWP {

// Invoke on the application's WinRT UI apartment. Completion may run on a
// different apartment; the caller must dispatch it to the engine run loop.
// HRESULTs are preserved so access/thread-affinity failures are observable.
HRESULT clear();
HRESULT writeText(const String&);
void readText(CompletionHandler<void(HRESULT, String)>&&);

// Install on the engine thread before clipboard operations. The application
// supplies its XAML/CoreDispatcher enqueue function: true means accepted;
// false means the task was not run. The dispatcher must preserve FIFO order.
WEBCORE_EXPORT void setUIDispatcher(Function<bool(Function<void()>&&)>&&);

// Engine-facing operations: enqueue on UI, then deliver completion on the
// caller's run loop. A missing dispatcher is an error, not an empty clipboard.
WEBCORE_EXPORT void clearAsync(CompletionHandler<void(HRESULT)>&&);
WEBCORE_EXPORT void writeTextAsync(const String&, CompletionHandler<void(HRESULT)>&&);
WEBCORE_EXPORT void readTextAsync(CompletionHandler<void(HRESULT, String)>&&);

} // namespace WebCore::ClipboardUWP
