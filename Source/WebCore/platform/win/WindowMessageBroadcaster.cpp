/*
 * Copyright (C) 2007, 2014 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 * 3.  Neither the name of Apple Inc. ("Apple") nor the names of
 *     its contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "WindowMessageBroadcaster.h"

#include "WindowMessageListener.h"

namespace WebCore {

using InstanceMap = HashMap<HWND, WindowMessageBroadcaster*>;

static InstanceMap& instancesMap()
{
    static InstanceMap instances;
    return instances;
}

void WindowMessageBroadcaster::addListener(HWND hwnd, WindowMessageListener* listener)
{
    WindowMessageBroadcaster* broadcaster = instancesMap().get(hwnd);
    if (!broadcaster) {
        broadcaster = new WindowMessageBroadcaster(hwnd);
        instancesMap().add(hwnd, broadcaster);
    }

    broadcaster->addListener(listener);
}

void WindowMessageBroadcaster::removeListener(HWND hwnd, WindowMessageListener* listener)
{
    WindowMessageBroadcaster* broadcaster = instancesMap().get(hwnd);
    if (!broadcaster)
        return;

    broadcaster->removeListener(listener);
}

WindowMessageBroadcaster::WindowMessageBroadcaster(HWND hwnd)
    : m_subclassedWindow(hwnd)
    , m_originalWndProc(0)
{
#if !PLATFORM(UWP)
    ASSERT_ARG(hwnd, IsWindow(hwnd));
#else
    // WebKitWebView: IsWindow (USER32) is desktop-only; no HWNDs on UWP.
    UNUSED_PARAM(hwnd);
#endif
}

WindowMessageBroadcaster::~WindowMessageBroadcaster() = default;

void WindowMessageBroadcaster::addListener(WindowMessageListener* listener)
{
    if (m_listeners.isEmpty()) {
        ASSERT(!m_originalWndProc);
#if PLATFORM(UWP)
        // WebKitWebView: SetWindowLongPtr (USER32) is desktop-only.
#else
#pragma warning(disable: 4244 4312)
        m_originalWndProc = reinterpret_cast<WNDPROC>(SetWindowLongPtr(m_subclassedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(SubclassedWndProc)));
#endif
    }
#if !PLATFORM(UWP)
    ASSERT(m_originalWndProc);
#endif

    m_listeners.add(listener);
}

void WindowMessageBroadcaster::removeListener(WindowMessageListener* listener)
{
    m_listeners.remove(listener);
    if (m_listeners.isEmpty())
        destroy();
}

void WindowMessageBroadcaster::destroy()
{
    m_listeners.clear();
    unsubclassWindow();
    instancesMap().remove(m_subclassedWindow);
    delete this;
}

void WindowMessageBroadcaster::unsubclassWindow()
{
#if !PLATFORM(UWP)
    SetWindowLongPtr(m_subclassedWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(m_originalWndProc));
#endif
    m_originalWndProc = 0;
}

LRESULT CALLBACK WindowMessageBroadcaster::SubclassedWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowMessageBroadcaster* broadcaster = instancesMap().get(hwnd);
    ASSERT(broadcaster);
    if (!broadcaster)
        return 0;

    ListenerSet::const_iterator end = broadcaster->listeners().end();
    for (ListenerSet::const_iterator it = broadcaster->listeners().begin(); it != end; ++it)
        (*it)->windowReceivedMessage(hwnd, message, wParam, lParam);

    WNDPROC originalWndProc = broadcaster->originalWndProc();

    // This will delete broadcaster.
    if (message == WM_DESTROY)
        broadcaster->destroy();

#if PLATFORM(UWP)
    // WebKitWebView: CallWindowProc (USER32) is desktop-only.
    UNUSED_PARAM(hwnd);
    UNUSED_PARAM(wParam);
    UNUSED_PARAM(lParam);
    return 0;
#else
    return CallWindowProc(originalWndProc, hwnd, message, wParam, lParam);
#endif
}

} // namespace WebCore
