#include "FrameSchedulerDriverUWP.h"

#include <atomic>
#include <limits>
#include <new>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Core;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Media;

namespace WebKit {
namespace {

// Owner service cadence in SDK 100-nanosecond units; unrelated to display refresh.
constexpr int64_t serviceTimerInterval100ns = 500000;
// One private process/owner driver; its ordinary last-client close revokes all hooks.
Agile<FrameSchedulerDriverUWP^> ownerFrameDriver;

} // namespace

// Reject a foreign dispatcher before acquiring process state or installing SDK hooks.
FrameSchedulerDriverUWP^ FrameSchedulerDriverUWP::Acquire(const WKUWPEngineAPI* api, CoreDispatcher^ dispatcher)
{
    if (!dispatcher || !dispatcher->HasThreadAccess) throw ref new WrongThreadException();
    std::shared_ptr<OwnerThreadServicePumpUWP> pump;
    auto result = OwnerThreadServicePumpUWP::acquire(api, pump);
    if (FAILED(result)) throw Exception::CreateException(result);
    auto driver = ownerFrameDriver.Get();
    if (driver && !driver->m_stopped) return driver;
    driver = ref new FrameSchedulerDriverUWP(dispatcher, pump);
    ownerFrameDriver = driver;
    return driver;
}

// Install one weak owner timer and one Rendering hook without rooting any control.
FrameSchedulerDriverUWP::FrameSchedulerDriverUWP(CoreDispatcher^ dispatcher, const std::shared_ptr<OwnerThreadServicePumpUWP>& pump)
    : m_dispatcher(dispatcher)
    , m_pump(pump)
    , m_timer(ref new DispatcherTimer())
{
    WeakReference weak(this);
    m_timer->Interval = TimeSpan { serviceTimerInterval100ns };
    m_tickToken = m_timer->Tick += ref new EventHandler<Object^>([weak](Object^, Object^) {
        auto driver = weak.Resolve<FrameSchedulerDriverUWP>();
        if (driver) driver->DispatchService(true);
    });
    m_renderToken = CompositionTarget::Rendering += ref new EventHandler<Object^>([weak](Object^, Object^ args) {
        auto driver = weak.Resolve<FrameSchedulerDriverUWP>();
        if (!driver) return;
        try { driver->DispatchFrame(safe_cast<RenderingEventArgs^>(args)->RenderingTime.Duration); }
        catch (Exception^ exception) { driver->m_lastResult = exception->HResult; }
        catch (const std::bad_alloc&) { driver->m_lastResult = E_OUTOFMEMORY; }
        catch (...) { driver->m_lastResult = E_FAIL; }
    });
}

// All hook/client state is owner-thread data, including observable diagnostics.
void FrameSchedulerDriverUWP::CheckThread()
{
    if (!m_dispatcher->HasThreadAccess) throw ref new WrongThreadException();
}

// Prepare callback allocations before adopting the native live service lease.
uint64_t FrameSchedulerDriverUWP::AddClient(const std::shared_ptr<FrameSchedulerUWP>& scheduler, std::function<HRESULT(int64_t)>&& frame)
{
    CheckThread();
    if (m_stopped) throw ref new ObjectDisposedException();
    if (!scheduler) throw ref new InvalidArgumentException();
    if (m_nextClient == (std::numeric_limits<uint64_t>::max)()) throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    auto entry = std::make_shared<Client>(Client { scheduler, std::move(frame) });
    auto token = ++m_nextClient;
    m_clients.emplace(token, entry);
    auto result = scheduler->attach(m_pump);
    if (FAILED(result)) { m_clients.erase(token); throw Exception::CreateException(result); }
    try { m_timer->Start(); }
    catch (...) { scheduler->close(); m_clients.erase(token); throw; }
    return token;
}

// Retained dispatch snapshots stay valid while the closed scheduler rejects further callbacks.
void FrameSchedulerDriverUWP::RemoveClient(uint64_t token)
{
    CheckThread();
    auto found = m_clients.find(token);
    if (found == m_clients.end()) return;
    found->second->scheduler->close();
    m_clients.erase(found);
    if (m_clients.empty()) StopHooks();
}

// Revoke SDK hooks on ordinary last-client removal without affecting a peer engine/page.
void FrameSchedulerDriverUWP::StopHooks()
{
    if (m_stopped) return;
    m_stopped = true;
    m_timer->Stop();
    m_timer->Tick -= m_tickToken;
    CompositionTarget::Rendering -= m_renderToken;
    ownerFrameDriver = nullptr;
}

// Keep timer/wake callbacks nonthrowing while retaining the exact original failure.
void FrameSchedulerDriverUWP::DispatchService(bool timer)
{
    try {
        CheckThread();
        if (m_stopped) return;
        if (timer) ++m_timerDispatches;
        auto result = m_pump->serviceNow();
        if (FAILED(result)) m_lastResult = result;
    } catch (Exception^ exception) { m_lastResult = exception->HResult; }
    catch (const std::bad_alloc&) { m_lastResult = E_OUTOFMEMORY; }
    catch (...) { m_lastResult = E_FAIL; }
}

// Snapshot weak-target ports and service once per epoch before each eligible view frame.
void FrameSchedulerDriverUWP::DispatchFrame(int64_t epoch)
{
    CheckThread();
    if (m_stopped || m_rendering) return;
    FrameSchedulerDriverUWP^ lifetime = this;
    std::vector<std::shared_ptr<Client>> clients;
    for (auto& entry : m_clients) clients.push_back(entry.second);
    m_rendering = true;
    for (auto& client : clients) {
        try {
            auto result = client->scheduler->beginFrame(epoch);
            if (result == S_OK && client->frame) result = client->frame(epoch);
            if (FAILED(result) && result != RO_E_CLOSED) m_lastResult = result;
        } catch (Exception^ exception) { m_lastResult = exception->HResult; }
        catch (const std::bad_alloc&) { m_lastResult = E_OUTOFMEMORY; }
        catch (...) { m_lastResult = E_FAIL; }
    }
    m_rendering = false;
    (void)lifetime;
}

// Worker calls retain only a weak driver and its owner dispatcher through enqueue/unwind.
HRESULT FrameSchedulerDriverUWP::RequestWake()
{
    if (m_wakePending.exchange(true)) return S_OK;
    try {
        WeakReference weak(this);
        m_dispatcher->RunAsync(CoreDispatcherPriority::Normal, ref new DispatchedHandler([weak] {
            auto driver = weak.Resolve<FrameSchedulerDriverUWP>();
            if (!driver) return;
            driver->m_wakePending.store(false);
            ++driver->m_wakeDispatches;
            driver->DispatchService(false);
        }));
        return S_OK;
    } catch (Exception^ exception) { m_wakePending.store(false); return exception->HResult; }
    catch (const std::bad_alloc&) { m_wakePending.store(false); return E_OUTOFMEMORY; }
    catch (...) { m_wakePending.store(false); return E_FAIL; }
}

// Explicit suspension pauses process service; unloaded visuals do not use this path.
HRESULT FrameSchedulerDriverUWP::SetSuspended(bool suspended)
{
    CheckThread();
    if (m_stopped) return RO_E_CLOSED;
    auto result = m_pump->setSuspended(suspended);
    if (SUCCEEDED(result) && !suspended) DispatchService(false);
    return result;
}

} // namespace WebKit
