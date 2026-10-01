/*
 * Copyright (C) 2006, 2007 Apple Inc. All rights reserved.
 * Copyright (C) 2009 Torch Mobile, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "config.h"
#include "PlatformScreen.h"

#include "DestinationColorSpace.h"
#include "FloatRect.h"
#include "HostWindow.h"
#include "IntRect.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "NotImplemented.h"
#include <windows.h>

namespace WebCore {

#if PLATFORM(UWP)
// WebKitWebView: USER32 monitor APIs (MonitorFromWindow, EnumDisplaySettings)
// are desktop-only. v0 reports a fixed 950 XL logical rect; the M4 XAML layer
// wires the real size later.
static const FloatRect defaultUwpScreenRect(0, 0, 480, 853);
#endif

// Returns info for the default monitor if widget is NULL
#if !PLATFORM(UWP)
static MONITORINFOEX monitorInfoForWidget(Widget* widget)
{
    HWND window = widget ? widget->root()->hostWindow()->platformPageClient() : 0;
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFOEX monitorInfo;
    monitorInfo.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(monitor, &monitorInfo);
    return monitorInfo;
}

static DEVMODE deviceInfoForWidget(Widget* widget)
{
    DEVMODE deviceInfo;
    deviceInfo.dmSize = sizeof(DEVMODE);
    deviceInfo.dmDriverExtra = 0;
    MONITORINFOEX monitorInfo = monitorInfoForWidget(widget);
    EnumDisplaySettings(monitorInfo.szDevice, ENUM_CURRENT_SETTINGS, &deviceInfo);

    return deviceInfo;
}
#endif

int screenDepth(Widget* widget)
{
#if PLATFORM(UWP)
    UNUSED_PARAM(widget);
    return 24;
#else
    DEVMODE deviceInfo = deviceInfoForWidget(widget);
    if (deviceInfo.dmBitsPerPel == 32) {
        // Some video drivers return 32, but this function is supposed to ignore the alpha
        // component. See <http://webkit.org/b/42972>.
        return 24;
    }
    return deviceInfo.dmBitsPerPel;
#endif
}

int screenDepthPerComponent(Widget* widget)
{
    // FIXME: Assumes RGB -- not sure if this is right.
    return screenDepth(widget) / 3;
}

bool screenIsMonochrome(Widget* widget)
{
#if PLATFORM(UWP)
    UNUSED_PARAM(widget);
    return false;
#else
    DEVMODE deviceInfo = deviceInfoForWidget(widget);
    return deviceInfo.dmColor == DMCOLOR_MONOCHROME;
#endif
}

bool screenHasInvertedColors()
{
    return false;
}

FloatRect screenRect(Widget* widget)
{
#if PLATFORM(UWP)
    UNUSED_PARAM(widget);
    return defaultUwpScreenRect;
#else
    MONITORINFOEX monitorInfo = monitorInfoForWidget(widget);
    return monitorInfo.rcMonitor;
#endif
}

FloatRect screenAvailableRect(Widget* widget)
{
#if PLATFORM(UWP)
    UNUSED_PARAM(widget);
    return defaultUwpScreenRect;
#else
    MONITORINFOEX monitorInfo = monitorInfoForWidget(widget);
    return monitorInfo.rcWork;
#endif
}

DestinationColorSpace screenColorSpace(Widget*)
{
    return DestinationColorSpace::SRGB();
}

bool screenSupportsExtendedColor(Widget*)
{
    return false;
}

} // namespace WebCore
