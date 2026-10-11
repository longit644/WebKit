// Native input laws: gesture classification, scroll physics and V1 input forwarding.
#include "input/InputAdapterW10M.h"
#include "native/PageControllerW10M.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

using namespace WebKit;

namespace {

// Fail-closed totals across all independent input cases.
unsigned checks = 0, failures = 0;
// Diagnostic stages emitted through controller report sinks.
std::vector<std::string> stages;

// Preserve an earlier failure independently of later passing checks.
void check(bool value, const char* name)
{
    ++checks;
    if (!value)
        ++failures;
    std::printf("%s: %s\n", value ? "PASS" : "FAIL", name);
}

// Record scroll-physics diagnostic stages for ordering assertions.
void recordStage(const char* stage) { stages.emplace_back(stage); }

// Controllable document/scroll engine double with exact call observation.
struct EngineDouble : ScrollEngineW10M {
    // Current offset, document and viewport heights in CSS pixels.
    int scrollY { 0 }, contentHeight { 1000 }, viewportHeight { 100 };
    // Forwarded engine scroll calls.
    unsigned scrollCalls { 0 };

    // Apply the caller's delta verbatim; clamping belongs to the controller.
    HRESULT scroll(int, int dy) override
    {
        ++scrollCalls;
        scrollY += dy;
        return S_OK;
    }

    // Snapshot live geometry for target clamping.
    HRESULT snapshot(ScrollSnapshotW10M& output) override
    {
        output = { scrollY, contentHeight, viewportHeight };
        return S_OK;
    }
};

// Tap double recording exact press/release coordinates.
struct TapDouble : TapEngineW10M {
    // Copied tap points and completed pair count.
    double pressX { 0 }, pressY { 0 }, releaseX { 0 }, releaseY { 0 };
    unsigned pairs { 0 };

    // Record one tap press without touching document state.
    HRESULT tapPress(double x, double y) override
    {
        pressX = x;
        pressY = y;
        return S_OK;
    }

    // Record the matching release and complete one observable pair.
    HRESULT tapRelease(double x, double y) override
    {
        releaseX = x;
        releaseY = y;
        ++pairs;
        return S_OK;
    }
};

// Controlled V1 input provider recording forwarded events and focus calls.
struct InputProvider {
    // Negotiated table presented to the controller.
    WKW10MEngineAPI api { };
    // Last forwarded event copy and focus value.
    WKW10MInputEvent last { };
    unsigned inputCalls { 0 }, focusCalls { 0 }, lastFocus { 0 };
    // Controlled originating results.
    WKW10MResult inputResult { WKW10MResultOK }, focusResult { WKW10MResultOK };

    // Build a capable table with controllable input/focus slots.
    InputProvider(bool inputCapable)
    {
        api.header = { sizeof(api), WKW10M_STRUCTURE_VERSION };
        api.abiMajor = WKW10M_ABI_MAJOR_VERSION;
        api.capabilities = WKW10MCapabilityStructuredNotifications | WKW10MCapabilityHostHtml
            | WKW10MCapabilityCertificateDecisions | (inputCapable ? WKW10MCapabilityInput : 0);
        api.setCallbacks = &setCallbacks;
        api.navigate = &navigate;
        api.loadHtml = &loadHtml;
        api.goBack = &noop;
        api.goForward = &noop;
        api.reload = &noop;
        api.stop = &stop;
        api.proceedWithCertificateFailure = &retry;
        api.closePage = &close;
        api.sendInput = &sendInput;
        api.setFocus = &setFocus;
        providers[nextPage] = this;
        page = nextPage++;
    }

    // Remove the registry entry; the controller never owns the provider.
    ~InputProvider() { providers.erase(page); }

    // Route by explicit page handle for the context-free table signatures.
    static WKW10MPageHandle nextPage;
    static struct Registry : std::map<WKW10MPageHandle, InputProvider*> { } providers;
    WKW10MPageHandle page { 0 };

    // Locate only the explicitly registered provider page.
    static InputProvider* find(WKW10MPageHandle page)
    {
        auto item = providers.find(page);
        return item == providers.end() ? nullptr : item->second;
    }

    // Accept the retained callback attachment without recordingance.
    static WKW10MResult __stdcall setCallbacks(WKW10MPageHandle, const WKW10MPageCallbacks*) { return WKW10MResultOK; }
    // Unused navigation/history slots answer success without side effects.
    // Unused navigation slot answers success without side effects.
    static WKW10MResult __stdcall navigate(WKW10MPageHandle, const WKW10MNavigationRequest*, uint64_t*) { return WKW10MResultOK; }
    // Unused HTML slot answers success without side effects.
    static WKW10MResult __stdcall loadHtml(WKW10MPageHandle, const WKW10MNavigationRequest*, uint64_t*) { return WKW10MResultOK; }
    // Unused history/stop slots answer success without side effects.
    static WKW10MResult __stdcall noop(WKW10MPageHandle) { return WKW10MResultOK; }
    // Unused stop slot answers success without side effects.
    static WKW10MResult __stdcall stop(WKW10MPageHandle) { return WKW10MResultOK; }
    // Unused retry slot answers success without side effects.
    static WKW10MResult __stdcall retry(WKW10MPageHandle, const WKW10MCertificateToken*, uint64_t*) { return WKW10MResultOK; }
    // Unused close slot answers success without side effects.
    static WKW10MResult __stdcall close(WKW10MPageHandle) { return WKW10MResultOK; }

    // Copy the validated event and answer the controlled result.
    static WKW10MResult __stdcall sendInput(WKW10MPageHandle page, const WKW10MInputEvent* event)
    {
        auto self = find(page);
        if (!self || !event)
            return WKW10MResultClosed;
        ++self->inputCalls;
        self->last = *event;
        return self->inputResult;
    }

    // Record the focus value and answer the controlled result.
    static WKW10MResult __stdcall setFocus(WKW10MPageHandle page, uint32_t focused)
    {
        auto self = find(page);
        if (!self)
            return WKW10MResultClosed;
        ++self->focusCalls;
        self->lastFocus = focused;
        return self->focusResult;
    }
};

WKW10MPageHandle InputProvider::nextPage = 501;
InputProvider::Registry InputProvider::providers;

// Fresh gesture/physics pair with a recording diagnostic sink.
struct Fixture {
    InputAdapterW10M input;
    ScrollControllerW10M scroller;
    EngineDouble engine;
    TapDouble taps;

    // Fresh gesture/physics pair wires the recording diagnostic sink on construction.
    Fixture() { scroller.setReportSink(recordStage); }
};

// Build one valid pointer event for the forwarding tests.
WKW10MInputEvent pointerEvent(uint32_t kind = WKW10MInputPointerDown)
{
    WKW10MInputEvent event { };
    event.header = { sizeof(event), WKW10M_STRUCTURE_VERSION };
    event.kind = kind;
    event.pointerId = 7;
    event.xCssPixels = 100;
    event.yCssPixels = 200;
    return event;
}

// Bind one controller to the provider's table for input forwarding checks.
HRESULT bindController(PageControllerW10M& controller, InputProvider& provider)
{
    WKW10MPageCallbacks callbacks { };
    return controller.bind(&provider.api, provider.page, &callbacks);
}

// Check tap classification, drag/scroll, release and abort behavior.
void runGestures()
{
    stages.clear();
    {
        Fixture f;
        PointerReleaseW10M outcome { };
        check(f.input.press(100, 200, 1.0, f.scroller) == S_OK, "press begins tracking");
        check(f.input.release(102, 203, 1.1, f.scroller, f.taps, outcome) == S_OK && outcome.tapped
                && !outcome.inertiaStarted && f.taps.pairs == 1 && f.taps.pressX == 102 && f.taps.pressY == 203
                && !f.engine.scrollCalls,
            "release inside slop taps the engine at release coordinates without scrolling or fling");
    }
    {
        Fixture f;
        PointerReleaseW10M outcome { };
        f.input.press(100, 500, 1.0, f.scroller);
        for (int i = 0; i < 10; ++i)
            f.input.move(100, 500 - (i + 1) * 100.0, 1.0 + (i + 1) * 0.016, f.scroller);
        bool scrolled = false;
        f.scroller.advance(1.2, true, f.engine, scrolled);
        check(f.engine.scrollY == 900 && f.scroller.overscrollY() < 0, "past-edge drag holds presentation overscroll");
        f.input.release(100, -500, 1.3, f.scroller, f.taps, outcome);
        check(!outcome.tapped, "edge release performs no tap");
        f.input.press(100, 180, 2.0, f.scroller);
        PointerReleaseW10M tapOutcome { };
        f.input.release(100, 180, 2.1, f.scroller, f.taps, tapOutcome);
        check(tapOutcome.tapped && f.taps.pairs == 1 && f.taps.pressY == 180 - f.scroller.overscrollY(),
            "tap release rides above held edge displacement");
    }
    {
        Fixture f;
        PointerReleaseW10M outcome { };
        f.input.press(100, 250, 1.0, f.scroller);
        f.input.move(100, 200, 1.05, f.scroller);
        check(f.input.dragging(), "motion beyond slop becomes a drag");
        bool scrolled = false;
        check(f.scroller.advance(1.06, true, f.engine, scrolled) == S_OK && scrolled && f.engine.scrollY == 50,
            "drag delta reaches the engine exactly once on the next frame");
        f.input.release(100, 200, 1.3, f.scroller, f.taps, outcome);
        check(!outcome.tapped && !f.taps.pairs, "drag release never synthesizes a tap");
    }
    {
        Fixture f;
        f.input.press(100, 200, 1.0, f.scroller);
        f.input.move(105, 205, 1.05, f.scroller);
        bool scrolled = true;
        f.scroller.advance(1.06, true, f.engine, scrolled);
        check(!f.input.dragging() && !scrolled && !f.engine.scrollCalls, "slop motion is classification only, never document motion");
    }
    {
        Fixture f;
        f.input.move(10, 10, 1.0, f.scroller);
        PointerReleaseW10M outcome { };
        check(f.input.release(10, 10, 1.0, f.scroller, f.taps, outcome) == S_OK && !outcome.tapped,
            "motion and release without press are ignored");
    }
    {
        Fixture f;
        PointerReleaseW10M outcome { };
        f.input.press(100, 400, 1.0, f.scroller);
        f.input.move(100, 300, 1.01, f.scroller);
        f.input.move(100, 200, 1.02, f.scroller);
        f.input.release(100, 200, 1.03, f.scroller, f.taps, outcome);
        check(outcome.inertiaStarted && f.scroller.hasInertia(), "fast recent drag arms a fling on release");
        stages.clear();
        int settled = f.engine.scrollY;
        for (int i = 0; i < 200; ++i) {
            bool scrolled = false;
            f.scroller.advance(1.04 + i * 0.016, false, f.engine, scrolled);
        }
        check(f.engine.scrollY > settled && !f.scroller.hasInertia(), "fling coasts the document then stops");
        bool stopped = false;
        for (auto& stage : stages)
            stopped = stopped || stage == "control-inertia-stop";
        check(stopped, "completed fling reports its stop stage once");
    }
    {
        Fixture f;
        f.input.press(100, 300, 1.0, f.scroller);
        f.input.move(100, 290, 1.5, f.scroller);
        PointerReleaseW10M outcome { };
        f.input.release(100, 290, 2.0, f.scroller, f.taps, outcome);
        check(!outcome.inertiaStarted && !f.scroller.hasInertia(), "slow stale release never flings");
    }
    {
        Fixture f;
        f.input.press(100, 200, 1.0, f.scroller);
        f.input.move(100, 100, 1.02, f.scroller);
        check(f.input.abort(f.scroller) == S_OK && !f.input.pointerDown(), "capture cancel ends tracking");
        bool scrolled = true;
        f.scroller.advance(1.03, false, f.engine, scrolled);
        check(!scrolled && !f.engine.scrollCalls, "cancelled drag leaves no queued motion");
    }
    {
        Fixture f;
        check(InputAdapterW10M::wheelDeltaToCssPixels(120) == -80 && InputAdapterW10M::wheelDeltaToCssPixels(-240) == 160,
            "wheel units map one notch to eighty CSS pixels with inverted sign");
        f.engine.scrollY = 900;
        check(f.scroller.addWheelDelta(500) == S_OK, "wheel queues plain motion");
        bool scrolled = true;
        stages.clear();
        check(f.scroller.advance(1.0, false, f.engine, scrolled) == S_OK && !scrolled && !f.engine.scrollCalls
                && f.scroller.overscrollY() == 0,
            "wheel motion clamps at the edge without elastic overscroll or engine call");
    }
    {
        Fixture f;
        stages.clear();
        f.input.press(100, 900, 1.0, f.scroller);
        for (int i = 0; i < 30; ++i)
            f.input.move(100, 900 - (i + 1) * 50.0, 1.0 + (i + 1) * 0.016, f.scroller);
        bool scrolled = false;
        f.scroller.advance(1.6, true, f.engine, scrolled);
        check(f.engine.scrollY == 900 && f.scroller.overscrollY() < 0, "past-edge drag stretches presentation without moving the document");
        bool hit = false;
        for (auto& stage : stages)
            hit = hit || stage == "control-edge-hit";
        check(hit, "first edge contact reports exactly once per stretch");
        PointerReleaseW10M outcome { };
        f.input.release(100, -600, 1.7, f.scroller, f.taps, outcome);
        for (int i = 0; i < 400; ++i) {
            bool unused = false;
            f.scroller.advance(1.71 + i * 0.016, false, f.engine, unused);
        }
        check(f.scroller.overscrollY() == 0, "released edge spring settles back to zero");
        bool returned = false;
        for (auto& stage : stages)
            returned = returned || stage == "control-edge-returned";
        check(returned, "settled spring reports its return");
    }
    {
        Fixture f;
        check(f.input.press(NAN, 0, 1.0, f.scroller) == E_INVALIDARG
                && f.input.move(0, 0, 1.0, f.scroller) == S_OK
                && f.scroller.addDragDelta(0, INFINITY) == E_INVALIDARG,
            "nonfinite coordinates reject before gesture state changes");
    }
    {
        Fixture a, b;
        PointerReleaseW10M outcome { };
        a.input.press(50, 150, 1.0, a.scroller);
        a.input.move(50, 50, 1.02, a.scroller);
        b.input.press(300, 300, 1.0, b.scroller);
        b.input.release(300, 300, 1.1, b.scroller, b.taps, outcome);
        bool scrolled = false;
        a.scroller.advance(1.03, true, a.engine, scrolled);
        check(outcome.tapped && b.taps.pairs == 1 && a.engine.scrollY == 100 && !b.engine.scrollCalls,
            "two interleaved controls keep independent gesture and scroll state");
    }
    {
        Fixture f;
        f.input.press(1, 1, 1.0, f.scroller);
        unsigned wrongThread = 0;
        std::thread worker([&] {
            PointerReleaseW10M outcome { };
            wrongThread += f.input.press(0, 0, 2.0, f.scroller) == RPC_E_WRONG_THREAD;
            wrongThread += f.input.move(0, 0, 2.0, f.scroller) == RPC_E_WRONG_THREAD;
            wrongThread += f.input.release(0, 0, 2.0, f.scroller, f.taps, outcome) == RPC_E_WRONG_THREAD;
            wrongThread += f.input.abort(f.scroller) == RPC_E_WRONG_THREAD;
            wrongThread += f.scroller.addDragDelta(1, 1) == RPC_E_WRONG_THREAD;
            wrongThread += f.scroller.reset(2.0) == RPC_E_WRONG_THREAD;
            bool unused = false;
            wrongThread += f.scroller.advance(2.0, false, f.engine, unused) == RPC_E_WRONG_THREAD;
        });
        worker.join();
        check(wrongThread == 7 && f.input.pointerDown(), "worker mutations reject before gesture or physics state changes");
    }
    {
        Fixture f;
        f.input.press(100, 100, 1.0, f.scroller);
        f.input.move(100, 200, 1.02, f.scroller);
        check(f.scroller.reset(2.0) == S_OK, "navigation resets physics");
        bool scrolled = true;
        f.scroller.advance(2.01, false, f.engine, scrolled);
        check(!scrolled && !f.engine.scrollCalls && !f.scroller.hasInertia(), "reset drops queued motion and coasting");
    }
}

// Check V1 keyboard/focus validation, threading and explicit engine support.
void runForwarding()
{
    {
        PageControllerW10M controller;
        auto invalid = pointerEvent();
        invalid.header.structureSize = 4;
        check(controller.sendInput(invalid) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "unbound input rejects before validation");
        check(controller.setFocused(true) == HRESULT_FROM_WIN32(ERROR_NOT_READY), "unbound focus rejects before slot access");
    }
    {
        InputProvider provider(false);
        PageControllerW10M controller;
        check(bindController(controller, provider) == S_OK, "input test table binds without the input capability");
        auto badSize = pointerEvent();
        badSize.header.structureSize = 4;
        check(controller.sendInput(badSize) == E_INVALIDARG, "short table prefix rejects before slot access");
        auto badVersion = pointerEvent();
        badVersion.header.structureVersion = 99;
        check(controller.sendInput(badVersion) == E_INVALIDARG, "unknown structure version rejects");
        auto badKind = pointerEvent(99);
        check(controller.sendInput(badKind) == E_INVALIDARG, "unknown input kind rejects");
        auto badModifiers = pointerEvent();
        badModifiers.modifiers = 0x10;
        check(controller.sendInput(badModifiers) == E_INVALIDARG, "undefined modifier bits reject");
        auto badCoords = pointerEvent();
        badCoords.xCssPixels = NAN;
        check(controller.sendInput(badCoords) == E_INVALIDARG, "nonfinite pointer coordinates reject");
        auto noKey = pointerEvent(WKW10MInputKeyDown);
        check(controller.sendInput(noKey) == E_INVALIDARG, "key event without a virtual key rejects");
        auto surrogate = pointerEvent(WKW10MInputKeyDown);
        surrogate.virtualKey = 65;
        surrogate.unicodeScalar = 0xD800;
        check(controller.sendInput(surrogate) == E_INVALIDARG, "surrogate scalar rejects");
        auto tooLarge = pointerEvent(WKW10MInputKeyDown);
        tooLarge.virtualKey = 65;
        tooLarge.unicodeScalar = 0x110000;
        check(controller.sendInput(tooLarge) == E_INVALIDARG, "out-of-range scalar rejects");
        check(controller.sendInput(pointerEvent()) == E_NOTIMPL, "pointer without the input capability is explicitly unsupported");
        check(controller.setFocused(true) == E_NOTIMPL, "focus without engine delivery is explicitly unsupported");
        check(!provider.inputCalls && !provider.focusCalls, "rejected calls never reach provider slots");
    }
    {
        InputProvider provider(true);
        PageControllerW10M controller;
        check(bindController(controller, provider) == S_OK, "input-capable table binds");
        auto press = pointerEvent(WKW10MInputPointerDown);
        press.modifiers = WKW10MModifierShift;
        check(controller.sendInput(press) == S_OK && provider.inputCalls == 1 && provider.last.kind == WKW10MInputPointerDown
                && provider.last.pointerId == 7 && provider.last.xCssPixels == 100 && provider.last.modifiers == WKW10MModifierShift,
            "capable pointer event forwards its exact copied fields");
        auto key = pointerEvent(WKW10MInputKeyDown);
        key.virtualKey = 65;
        key.unicodeScalar = 65;
        check(controller.sendInput(key) == S_OK && provider.inputCalls == 2 && provider.last.virtualKey == 65,
            "capable key event forwards virtual key and scalar");
        auto wheel = pointerEvent(WKW10MInputWheel);
        wheel.wheelDeltaX = -3;
        wheel.wheelDeltaY = 9;
        check(controller.sendInput(wheel) == S_OK && provider.last.wheelDeltaY == 9, "capable wheel forwards both integer deltas");
        check(controller.setFocused(true) == S_OK && provider.focusCalls == 1 && provider.lastFocus == 1, "focus true forwards once");
        check(controller.setFocused(false) == S_OK && provider.lastFocus == 0, "focus false forwards explicitly");
        provider.inputResult = static_cast<WKW10MResult>(E_ACCESSDENIED);
        check(controller.sendInput(press) == E_ACCESSDENIED, "originating provider HRESULT survives forwarding");
        provider.inputResult = WKW10MResultOK;
    }
    {
        InputProvider slotless(true);
        slotless.api.sendInput = nullptr;
        slotless.api.setFocus = nullptr;
        PageControllerW10M controller;
        check(bindController(controller, slotless) == S_OK, "slotless table still binds its navigation surface");
        check(controller.sendInput(pointerEvent()) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH), "missing input slot rejects without calling");
        check(controller.setFocused(true) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH), "missing focus slot rejects without calling");
        check(!slotless.inputCalls && !slotless.focusCalls, "slotless calls never reach the provider");
    }
    {
        InputProvider provider(true);
        PageControllerW10M controller;
        bindController(controller, provider);
        unsigned wrongThread = 0;
        std::thread worker([&] {
            wrongThread += controller.sendInput(pointerEvent()) == RPC_E_WRONG_THREAD;
            wrongThread += controller.setFocused(true) == RPC_E_WRONG_THREAD;
        });
        worker.join();
        check(wrongThread == 2 && !provider.inputCalls && !provider.focusCalls, "worker input rejects before provider access");
    }
}

} // namespace

// Run native gesture/physics/forwarding laws without any XAML or engine image.
int main()
{
    runGestures();
    runForwarding();
    std::printf("M10 input contract: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
