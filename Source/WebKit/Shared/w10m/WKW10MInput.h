/*
 * WKW10MInput.h - frozen V1 input event contract.
 *
 * Companion to WKW10MEngine.h; see that header for the boundary rules. The
 * machine-readable authority is
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/native-abi.md.
 *
 * Coordinate contract: xCssPixels and yCssPixels are CSS pixels in page
 * viewport space with the origin at the top-left of the visible viewport. The
 * consumer applies the documented CSS-pixel-per-device-pixel ratio; the engine
 * converts to device pixels. Scale and visual offset are therefore never
 * expressed as fudge factors in the input path.
 */

#ifndef WKW10M_INPUT_H
#define WKW10M_INPUT_H

#include "WKW10MEngine.h"

#pragma pack(push, 4)

/* Event kinds delivered by the public control's XAML handlers. */
typedef enum {
    WKW10MInputKeyDown = 0,
    WKW10MInputKeyUp = 1,
    WKW10MInputPointerDown = 2,
    WKW10MInputPointerUp = 3,
    WKW10MInputPointerMove = 4,
    WKW10MInputWheel = 5
} WKW10MInputKind;

/*
 * Modifier bit set delivered with every event.
 *
 * Keys are the platform virtual-key values of the corresponding pressed keys, so
 * the engine can consult platform APIs directly and the public control never
 * needs a translation table. Zero means no modifier.
 */
typedef enum {
    WKW10MModifierAlt = 0x0001,
    WKW10MModifierControl = 0x0002,
    WKW10MModifierShift = 0x0004,
    WKW10MModifierWindows = 0x0008
} WKW10MModifier;

/*
 * One input event.
 *
 * Members unused by the event kind are zero. The engine neither retains the
 * event nor its spans past the call.
 */
typedef struct WKW10MInputEvent {
    WKW10MHeader header;

    /* WKW10MInputKind for this event. */
    uint32_t kind;

    /* Bitwise OR of WKW10MModifier values. */
    uint32_t modifiers;

    /* Stable per-pointer identifier from the platform; 0 for wheel and keyboard. */
    uint32_t pointerId;

    /* Platform virtual-key value for keyboard events; 0 otherwise. */
    uint32_t virtualKey;

    /* Unicode scalar from the platform for a text-producing key; 0 otherwise. */
    uint32_t unicodeScalar;

    /* Viewport-relative CSS-pixel X; ignored for keyboard events. */
    double xCssPixels;

    /* Viewport-relative CSS-pixel Y; ignored for keyboard events. */
    double yCssPixels;

    /* Horizontal wheel delta in CSS pixels; 0 for non-wheel events. */
    int32_t wheelDeltaX;

    /* Vertical wheel delta in CSS pixels; 0 for non-wheel events. */
    int32_t wheelDeltaY;
} WKW10MInputEvent;

#pragma pack(pop)

#endif /* WKW10M_INPUT_H */

