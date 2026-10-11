/*
 * WKW10MRendering.h - frozen V1 CPU paint and GPU compositor contracts.
 *
 * Companion to WKW10MEngine.h; see that header for the boundary rules. The
 * machine-readable authority is
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/native-abi.md.
 *
 * Presentation is exclusive by construction: a page has at most one active mode,
 * CPU or GPU. attachCompositor fails with WKW10MResultInvalidState while a CPU
 * surface is attached and paint fails with the same result while a compositor is
 * attached, so the documented one-WebKit-one-session rule cannot be violated by a
 * two-surface configuration.
 *
 * Threading: attach, detach and render are called on the owner thread with the
 * matching platform object still alive. The engine AddRefs whatever it retains
 * and releases it on detach or close, so the host never hands over ownership.
 */

#ifndef WKW10M_RENDERING_H
#define WKW10M_RENDERING_H

#include "WKW10MEngine.h"

#pragma pack(push, 4)

/*
 * CPU paint target.
 *
 * data is the start of a caller-owned buffer that must stay writable and
 * unmapped for the duration of the paint call. capacityBytes is 64-bit because
 * a 4K premultiplied BGRA frame is 33,177,600 bytes, which overflows 32 bits.
 *
 * Ownership of the backing store never moves: the caller allocates it, may
 * inspect it after the call, and is responsible for presenting it.
 */
typedef struct WKW10MPaintBuffer {
    WKW10MHeader header;

    /* Caller-owned buffer start; required, aligned to 16 bytes. */
    uint8_t* data;

    /* Caller-owned buffer extent in bytes; required. */
    uint64_t capacityBytes;

    /* Distance between rows in bytes; must be at least width * 4. */
    uint32_t strideBytes;

    /* Physical pixel width of the target. */
    uint32_t widthPixels;

    /* Physical pixel height of the target. */
    uint32_t heightPixels;
} WKW10MPaintBuffer;

/*
 * GPU compositor configuration.
 *
 * panelInspectable is the engine's IInspectable adaptation of the host panel;
 * the engine QIs it for the rendering interfaces it needs and AddRefs it. The
 * value is borrowed for the call only. scaleX and scaleY convert CSS pixels to
 * device pixels and normally equal the display's scale on both axes.
 */
typedef struct WKW10MCompositorOptions {
    WKW10MHeader header;

    /* Host panel Inspectable; required. */
    void* panelInspectable;

    /* Device pixels per CSS pixel; must be positive. */
    double scaleX;

    /* Device pixels per CSS pixel; must be positive. */
    double scaleY;
} WKW10MCompositorOptions;

#pragma pack(pop)

#endif /* WKW10M_RENDERING_H */

