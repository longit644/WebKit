/*
* Copyright (C) 2006, 2007, 2008 Apple Inc. All rights reserved.
* Copyright (C) 2007-2009 Torch Mobile, Inc.
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Library General Public License for more details.
*
* You should have received a copy of the GNU Library General Public License
* along with this library; see the file COPYING.LIB.  If not, write to
* the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301, USA.
*
*/

#include "config.h"
#include "Icon.h"

#include "GraphicsContext.h"
#include "LocalWindowsContext.h"
#include <windows.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

static const int shell32MultipleFileIconIndex = 54;

Icon::Icon(HICON icon)
    : m_hIcon(icon)
{
    ASSERT(icon);
}

Icon::~Icon()
{
#if !PLATFORM(UWP)
    DestroyIcon(m_hIcon);
#else
    // WebKitWebView: icons never exist on UWP (createIconForFiles returns null).
    UNUSED_PARAM(m_hIcon);
#endif
}

// FIXME: Move the code to ChromeClient::iconForFiles().
RefPtr<Icon> Icon::createIconForFiles(const Vector<String>& filenames)
{
#if PLATFORM(UWP)
    // WebKitWebView: SHGetFileInfo/ExtractIconEx (shell32) are desktop-only.
    UNUSED_PARAM(filenames);
    return nullptr;
#else
    if (filenames.isEmpty())
        return nullptr;

    if (filenames.size() == 1) {
        SHFILEINFO sfi;
        memset(&sfi, 0, sizeof(sfi));

        String tmpFilename = filenames[0];
        if (!SHGetFileInfo(tmpFilename.wideCharacters().span().data(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SHELLICONSIZE | SHGFI_SMALLICON))
            return nullptr;

        return adoptRef(new Icon(sfi.hIcon));
    }

    WCHAR buffer[MAX_PATH];
    UINT length = ::GetSystemDirectoryW(buffer, std::size(buffer));
    if (!length)
        return nullptr;

    if (wcscat_s(buffer, L"\\shell32.dll"))
        return nullptr;

    HICON hIcon;
    if (!::ExtractIconExW(buffer, shell32MultipleFileIconIndex, 0, &hIcon, 1))
        return nullptr;
    return adoptRef(new Icon(hIcon));
#endif
}

void Icon::paint(GraphicsContext& context, const FloatRect& r)
{
    if (context.paintingDisabled())
        return;

#if PLATFORM(UWP)
    // WebKitWebView: DrawIconEx (USER32) is desktop-only; icons never exist.
    UNUSED_PARAM(context);
    UNUSED_PARAM(r);
#else
    LocalWindowsContext windowContext(context, enclosingIntRect(r));
    DrawIconEx(windowContext.hdc(), r.x(), r.y(), m_hIcon, r.width(), r.height(), 0, 0, DI_NORMAL);
#endif
}

}
