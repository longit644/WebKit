# Proposal — wkview-m7-uwp-fonts: UWP font stack (fontconfig + FreeType + HarfBuzz)

## Why
The desktop WIN+CAIRO font path relies on GDI (HFONT/CreateFontIndirect, HDC/SelectObject/GetObject, DWrite-GDI interop) and Uniscribe shaping. UWP needs a compatible font backend to deliver text rendering for v0. This change selects the existing FreeType/fontconfig/HarfBuzz implementations and resolves related desktop-platform compilation paths. ARM32 WebCore DLL linkage was verified on 2026-10-01; runtime text rendering remains pending, as tracked in tasks.md.

## Decision: FreeType path, not DWrite-direct
- All deps proven: fontconfig (installed), FreeType (installed),
  HarfBuzz+icu (installed), cairo-ft (in our cairo build).
- In-tree reference: WPE/GTK FT port (FontPlatformData FcPattern +
  cairo_ft_face, HarfBuzzShaper). Copy patterns, not code.
- DWrite-direct rejected for v0: needs cairo-dwrite re-enabled (its
  cairo-dwrite-font.cpp is entangled with GDI blits + win32-font fallback —
  real surgery, no reference), then new WebCore DWrite cache code.
  DWrite returns in phase 2 via WebKit itself (recorded direction).

## Scope (11 files in target + headers)
- FontPlatformData.h: UWP branch (FcPattern + cairo_font_face, no
  GDIObject/HFONT/LOGFONT members).
- FontPlatformDataWinCairo.cpp / FontCacheWinCairo.cpp /
  FontCustomPlatformDataWinCairo.cpp: fontconfig enumeration + FT face
  creation (bundled fonts first, system enumeration second).
- ComplexTextControllerUniscribe.cpp: UWP uses HarfBuzz path (or guarded out;
  check what non-WIN ComplexTextController uses).
- FontMemoryResource.h / GDIObject.h / SharedGDIObject.h: UWP stubs
  (DeleteObject/AddFontMemResourceEx/RemoveFontMemResourceEx).
- FontCacheWin.cpp / FontCustomPlatformDataWin.cpp / FontWin.cpp /
  SimpleFontDataWin.cpp / FontDescriptionWin.cpp / SystemFontDatabaseWin.cpp:
  assess each — guard GDI bodies for UWP, keep FT-callable surface.
- USE(FREETYPE)=ON for the port (currently unset).

## Out of scope
- System font enumeration polish (bundled DejaVu first).
- DWrite backend (phase 2). Emoji/COLR layers (whatever FT gives).
