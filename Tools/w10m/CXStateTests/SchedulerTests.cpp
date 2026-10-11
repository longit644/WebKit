// Diagnostic-only real dispatcher/timer and actual native engine paused-resource oracle.
#include "WebKitWebView.h"
#include "platform/FrameSchedulerDriverW10M.h"
#include "../../../Source/WebCore/platform/w10m/PageW10M.h"

#include <cstdio>
#include <string>
#include <thread>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace WebKit;

namespace {

// Fixture CSS/timer/fetch creates exact distinct DOM content heights per page.
constexpr int expectedHeightA = 3001, expectedHeightB = 3002;
// Retained peer's independent JavaScript-timer result after the other page closes.
constexpr int expectedPeerHeight = 4002;
// Test watchdog in owner pulses; every missing callback/resource result fails closed.
constexpr unsigned maximumPulses = 100;
// Owner oracle observation interval in SDK 100-nanosecond units, separate from service.
constexpr int64_t oracleInterval100ns = 1000000;

// Retained asynchronous run is diagnostic-only and has no framework routed instance.
struct SchedulerRun;
// One suite owns native fixture log attachment until its explicit cleanup completes.
std::shared_ptr<SchedulerRun> activeSchedulerRun;

// Actual SDK-timer/engine run that remains alive through all owner callbacks and report writing.
struct SchedulerRun : std::enable_shared_from_this<SchedulerRun> {
    // Version-matched output path and configured fixture base copied before return.
    std::wstring path;
    std::string base;
    // Actual SDK action returned to the diagnostic host; readiness is not faked.
    InitializationActionUWP^ action;
    // Real public controls provide XAML lifetime/weak-target participants.
    WebKitWebView^ viewA { ref new WebKitWebView() };
    // Independent second real public XAML control, never a globally routed target.
    WebKitWebView^ viewB { ref new WebKitWebView() };
    // Shared private CX owner driver and separate native per-view eligibility.
    FrameSchedulerDriverUWP^ driver;
    std::shared_ptr<FrameSchedulerUWP> schedulerA { std::make_shared<FrameSchedulerUWP>() }, schedulerB { std::make_shared<FrameSchedulerUWP>() };
    // Shared service binding uses the real module's frozen service slot.
    std::shared_ptr<OwnerThreadServicePumpUWP> pump;
    // Native V1 and legacy diagnostic operation pointers; no engine code is linked.
    WKUWPEngineAPI api { };
    decltype(&WKCreatePageUWP) create { nullptr };
    decltype(&WKDestroyPageUWP) destroy { nullptr };
    decltype(&WKNavigatePageUWP) navigate { nullptr };
    decltype(&WKLoadHTMLUWP) html { nullptr };
    decltype(&WKPageNeedsPaintUWP) needsPaint { nullptr };
    decltype(&WKGetPageStateUWP) state { nullptr };
    decltype(&WKInstallClipboardDispatcherUWP) installDispatcher { nullptr };
    // Independently owned real native pages whose resources progress without paint/tick.
    void* pageA { nullptr }, *pageB { nullptr };
    // Driver participant identities survive copied callback snapshots until removal.
    uint64_t tokenA { 0 }, tokenB { 0 };
    // Real oracle timer and its weak run callback registration.
    DispatcherTimer^ oracle;
    EventRegistrationToken oracleToken;
    // Fail-closed totals plus retained per-assertion lines.
    unsigned checks { 0 }, failures { 0 };
    std::string lines;
    // Ordered test phase and bounded owner observation count.
    unsigned phase { 0 }, pulses { 0 };
    // Actual weak-view presentation callback counts, initially and finally paused.
    unsigned renderedA { 0 }, renderedB { 0 };
    // Original control/page terminal counts from diagnostic-only native sinks.
    unsigned completedA { 0 }, completedB { 0 };
    // Service/wake counters captured before explicit suspension and worker enqueue.
    uint64_t beforeSuspension { 0 }, beforeWake { 0 };
    // Worker mutation/wake results copied before the thread joins.
    unsigned workerChecks { 0 };
    // Cleanup/report/action settlement occurs once even on an unexpected failure.
    bool finished { false };

    // Append one independent result without overwriting a prior failed assertion.
    void check(bool value, const char* name)
    {
        ++checks;
        if (!value) ++failures;
        lines += value ? "PASS: " : "FAIL: "; lines += name; lines += "\r\n";
    }

    // Owner-native fixture sink A; logs are an oracle only, never a public event source.
    static void __stdcall logA(const char* name, unsigned long result)
    {
        auto run = activeSchedulerRun;
        if (run && std::string(name) == "navigation-completed" && !result) ++run->completedA;
        if (run) run->nativeLog("A", name, result);
    }

    // Owner-native fixture sink B, independent of coinciding local navigation identities.
    static void __stdcall logB(const char* name, unsigned long result)
    {
        auto run = activeSchedulerRun;
        if (run && std::string(name) == "navigation-completed" && !result) ++run->completedB;
        if (run) run->nativeLog("B", name, result);
    }

    // Capture bounded native response/error/resource-stage diagnostics without cookie contents.
    void nativeLog(const char* page, const char* name, unsigned long value)
    {
        std::string stage(name);
        if (stage.find("navigation-") != 0 && stage.find("uwp-load-") != 0) return;
        if (lines.size() > 16000) return;
        lines += page; lines += ": "; lines += stage; lines += "=" + std::to_string(value) + "\r\n";
    }

    // Borrow one legacy diagnostic operation from the pinned actual engine image.
    template<typename T> T operation(HMODULE engine, const char* name)
    {
        auto value = reinterpret_cast<T>(GetProcAddress(engine, name));
        if (!value) throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND));
        return value;
    }

    // Create actual engine pages and SDK hooks while both participants are visually paused.
    void start(const wchar_t* output, const wchar_t* fixture, InitializationActionUWP^ observation)
    {
        path = output; action = observation;
        auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fixture, -1, nullptr, 0, nullptr, nullptr);
        if (!count) throw ref new InvalidArgumentException();
        base.resize(count);
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, fixture, -1, &base[0], count, nullptr, nullptr);
        base.pop_back();
        auto engine = LoadPackagedLibrary(L"WebCore.dll", 0);
        if (!engine) throw Exception::CreateException(HRESULT_FROM_WIN32(GetLastError()));
        auto discover = operation<decltype(&WKUWPGetEngineAPI)>(engine, "WKUWPGetEngineAPI");
        api.header = { sizeof(api), WKUWP_STRUCTURE_VERSION };
        check(discover(1, 0, 0, &api) == WKUWPResultOK && api.service && api.setSuspended,
            "actual engine negotiation supplies process service and suspension slots");
        check(api.service() == WKUWPResultNotInitialized, "real service rejects before native engine initialization");
        create = operation<decltype(create)>(engine, "WKCreatePageUWP");
        destroy = operation<decltype(destroy)>(engine, "WKDestroyPageUWP");
        navigate = operation<decltype(navigate)>(engine, "WKNavigatePageUWP");
        html = operation<decltype(html)>(engine, "WKLoadHTMLUWP");
        needsPaint = operation<decltype(needsPaint)>(engine, "WKPageNeedsPaintUWP");
        state = operation<decltype(state)>(engine, "WKGetPageStateUWP");
        installDispatcher = operation<decltype(installDispatcher)>(engine, "WKInstallClipboardDispatcherUWP");
        auto package = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;
        auto fonts = package + L"\\Fonts";
        unsigned result = 0;
        pageA = create(fonts->Data(), 320, 240, &logA, &result);
        check(pageA && !result, "first actual native page initializes frozen process engine");
        pageB = create(fonts->Data(), 320, 240, &logB, &result);
        check(pageB && !result, "second actual native page shares setup and owns independent session");
        if (!pageA || !pageB) { finish(); return; }
        auto dispatcher = Window::Current->Dispatcher;
        check(SUCCEEDED(installDispatcher(pageA, reinterpret_cast<IInspectable*>(dispatcher)))
                && SUCCEEDED(installDispatcher(pageB, reinterpret_cast<IInspectable*>(dispatcher))),
            "two native pages reuse owner dispatcher wake instead of frame polling");
        driver = FrameSchedulerDriverUWP::Acquire(&api, dispatcher);
        auto same = FrameSchedulerDriverUWP::Acquire(&api, dispatcher);
        check(driver == same, "two acquisitions share one actual CX owner timer/rendering driver");
        std::weak_ptr<SchedulerRun> weak = shared_from_this();
        WeakReference weakA(viewA), weakB(viewB);
        tokenA = driver->AddClient(schedulerA, [weak, weakA](int64_t) {
            auto run = weak.lock();
            if (run && weakA.Resolve<WebKitWebView>()) ++run->renderedA;
            return S_OK;
        });
        tokenB = driver->AddClient(schedulerB, [weak, weakB](int64_t) {
            auto run = weak.lock();
            if (run && weakB.Resolve<WebKitWebView>()) ++run->renderedB;
            return S_OK;
        });
        pump = driver->ServicePump();
        check(pump->clientCount() == 2 && !schedulerA->presentationEnabled() && !schedulerB->presentationEnabled(),
            "real XAML participants retain service leases with all presentation disabled");
        auto ca = package + L"\\cacert.pem";
        check(!navigate(pageA, (base + "/scheduler.html?page=A").c_str(), ca->Data())
                && !navigate(pageB, (base + "/scheduler.html?page=B").c_str(), ca->Data()),
            "two actual pages request delayed CSS external script and timer-driven fetch");
        beforeSuspension = pump->serviceCount();
        check(driver->SetSuspended(true) == S_OK && driver->SetSuspended(true) == S_OK, "explicit app suspension is idempotent across shared participants");
        oracle = ref new DispatcherTimer();
        oracle->Interval = TimeSpan { oracleInterval100ns };
        oracleToken = oracle->Tick += ref new EventHandler<Object^>([weak](Object^, Object^) {
            auto run = weak.lock();
            if (!run) return;
            try { run->poll(); }
            catch (Exception^ exception) { run->check(false, "unexpected owner scheduler exception"); run->lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n"; run->finish(); }
            catch (...) { run->check(false, "unexpected native scheduler exception"); run->finish(); }
        });
        oracle->Start();
    }

    // Query actual DOM layout only; these operations never tick, paint or cycle the process loop.
    int height(void* page)
    {
        unsigned dirty = 0;
        WKPageStateUWP value { };
        return !needsPaint(page, &dirty) && !state(page, &value) ? value.contentHeight : -1;
    }

    // Observe the bounded asynchronous service/suspension/peer-close phases.
    void poll()
    {
        if (finished) return;
        if (++pulses > maximumPulses) {
            lines += "phase=" + std::to_string(phase) + " timers=" + std::to_string(driver->TimerDispatchCount())
                + " services=" + std::to_string(pump->serviceCount()) + " last=" + std::to_string(driver->LastResult())
                + " heightA=" + std::to_string(pageA ? height(pageA) : -1) + " heightB=" + std::to_string(pageB ? height(pageB) : -1)
                + " completedA=" + std::to_string(completedA) + " completedB=" + std::to_string(completedB) + "\r\n";
            check(false, "paused-resource scheduler watchdog expired"); finish(); return;
        }
        if (!phase) {
            if (pulses < 3) return;
            check(pump->serviceCount() == beforeSuspension && !renderedA && !renderedB,
                "explicit suspension prevents owner service and all per-view frames");
            check(driver->SetSuspended(false) == S_OK, "resume releases real loader deadline/queue suspension");
            phase = 1;
            return;
        }
        if (phase == 1) {
            if (height(pageA) != expectedHeightA || height(pageB) != expectedHeightB) return;
            check(completedA == 1 && completedB == 1, "two unloaded native pages finish their remote main loads once");
            check(height(pageA) == expectedHeightA && height(pageB) == expectedHeightB,
                "actual CSS script JavaScript timer and fetch produce distinct expected DOM heights without rendering");
            check(driver->TimerDispatchCount() > 0 && !renderedA && !renderedB && !schedulerA->frameCount() && !schedulerB->frameCount(),
                "SDK owner service timer progresses idle resources with zero page frame callbacks");
            schedulerA->setPresentationEnabled(true); schedulerB->setPresentationEnabled(true);
            auto count = pump->serviceCount();
            driver->DispatchFrame(0);
            check(pump->serviceCount() == count + 1 && renderedA == 1 && renderedB == 1,
                "one actual driver frame services once and dispatches both weak public-control targets");
            driver->DispatchFrame(0);
            check(pump->serviceCount() == count + 1 && renderedA == 1 && renderedB == 1,
                "same frame epoch cannot double actual engine service or participant delivery");
            schedulerA->setPresentationEnabled(false); schedulerB->setPresentationEnabled(false);
            viewA->Close(); driver->RemoveClient(tokenA); tokenA = 0;
            destroy(pageA); pageA = nullptr;
            check(pump->clientCount() == 1, "closing one control/page leaves peer owner service lease alive");
            check(!html(pageB, "<!doctype html><body style='margin:0'><div id='peer' style='height:10px'></div><script>setTimeout(function(){document.getElementById('peer').style.height='4002px';},350);</script>"),
                "surviving unloaded native peer schedules an independent delayed DOM mutation");
            phase = 2;
            return;
        }
        if (phase == 2) {
            if (height(pageB) != expectedPeerHeight) return;
            check(height(pageB) == expectedPeerHeight && renderedB == 1,
                "closed peer cannot interrupt remaining unloaded page JavaScript timer progress");
            beforeWake = driver->WakeDispatchCount();
            auto ownerDriver = driver;
            auto ownScheduler = schedulerB;
            std::thread worker([this, ownerDriver, ownScheduler] {
                workerChecks += ownerDriver->RequestWake() == S_OK;
                workerChecks += ownerDriver->RequestWake() == S_OK;
                workerChecks += ownScheduler->setPresentationEnabled(true) == RPC_E_WRONG_THREAD;
                workerChecks += api.service() == WKUWPResultWrongThread;
            });
            worker.join();
            phase = 3;
            return;
        }
        if (driver->WakeDispatchCount() == beforeWake) return;
        check(workerChecks == 4 && driver->WakeDispatchCount() == beforeWake + 1,
            "two worker wake requests coalesce once while foreign mutations and native service reject");
        check(SUCCEEDED(driver->LastResult()), "actual SDK service/rendering callbacks contain no hidden failure");
        finish();
    }

    // Revoke hooks/pages, write exact version/report, then settle the real host action once.
    void finish()
    {
        if (finished) return;
        finished = true;
        if (oracle) { oracle->Stop(); oracle->Tick -= oracleToken; }
        if (driver) {
            if (pump && pump->suspended()) driver->SetSuspended(false);
            if (tokenA) driver->RemoveClient(tokenA);
            if (tokenB) driver->RemoveClient(tokenB);
        }
        if (pageA && destroy) destroy(pageA);
        if (pageB && destroy) destroy(pageB);
        pageA = pageB = nullptr;
        viewA->Close(); viewB->Close();
        if (pump) check(!pump->clientCount(), "last ordinary client cleanup revokes shared SDK hooks and returns leases to baseline");
        char header[256];
        sprintf_s(header, "version=" TEST_PACKAGE_VERSION "\r\nresult=%s\r\nchecks=%u\r\nfailures=%u\r\n", failures ? "FAIL" : "PASS", checks, failures);
        lines.insert(0, header);
        auto file = CreateFile2(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
        HRESULT result = failures ? E_FAIL : S_OK;
        if (file == INVALID_HANDLE_VALUE) result = HRESULT_FROM_WIN32(GetLastError());
        else {
            DWORD written = 0;
            if (!WriteFile(file, lines.data(), static_cast<DWORD>(lines.size()), &written, nullptr) || written != lines.size()) result = E_FAIL;
            FlushFileBuffers(file); CloseHandle(file);
        }
        activeSchedulerRun.reset();
        if (action) { action->Resolve(result, false); action->Publish(); }
    }
};

} // namespace

// Diagnostic-only CX-to-CX SDK action entry; shipping builds exclude this export.
extern "C" __declspec(dllexport) IAsyncAction^ __stdcall WKUWPTestScheduler(const wchar_t* path, const wchar_t* fixtureBase)
{
    auto dispatcher = Window::Current->Dispatcher;
    auto action = ref new InitializationActionUWP(dispatcher, [](unsigned int) { });
    auto run = std::make_shared<SchedulerRun>();
    activeSchedulerRun = run;
    try { run->start(path, fixtureBase, action); }
    catch (Exception^ exception) { run->check(false, "scheduler setup failure"); run->lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n"; run->finish(); }
    catch (...) { run->check(false, "native scheduler setup failure"); run->finish(); }
    return action;
}
