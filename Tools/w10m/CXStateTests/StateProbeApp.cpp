#include <activation.h>
#include <windows.h>

using namespace Platform;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Data;

namespace WebKitStateProbe {

// Packaged CX host that embeds the real control and its live read-only bindings.
[Windows::Foundation::Metadata::WebHostHidden]
public ref class App sealed : Application {
public:
    // Construct the browser-free diagnostic Application.
    App() { }
protected:
    // Attach the real control/bindings, then run its suite once after Loaded.
    virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ args) override
    {
        (void)args;
        auto panel = ref new StackPanel();
        panel->Margin = Thickness(24);
        m_status = ref new TextBlock();
        m_status->FontSize = 28;
        m_status->Text = L"SOURCE / STATE TESTS " TEST_PACKAGE_VERSION_W;
        panel->Children->Append(m_status);
        m_title = ref new TextBlock();
        m_title->FontSize = 22;
        panel->Children->Append(m_title);
        m_back = ref new Button();
        m_back->Content = L"Bound CanGoBack";
        panel->Children->Append(m_back);
        m_view = ref new WebKit::WebKitWebView();
        m_view->Height = 40;
        panel->Children->Append(m_view);
        auto titleBinding = ref new Binding();
        titleBinding->Source = m_view;
        titleBinding->Path = ref new PropertyPath(L"DocumentTitle");
        m_title->SetBinding(TextBlock::TextProperty, titleBinding);
        auto historyBinding = ref new Binding();
        historyBinding->Source = m_view;
        historyBinding->Path = ref new PropertyPath(L"CanGoBack");
        m_back->SetBinding(Control::IsEnabledProperty, historyBinding);
        m_view->Loaded += ref new RoutedEventHandler([this](Object^, RoutedEventArgs^) {
            if (m_ran) return;
            m_ran = true;
            auto module = LoadPackagedLibrary(L"WebKit.dll", 0);
            using Run = HRESULT(__stdcall*)(IInspectable*, IInspectable*, IInspectable*, const wchar_t*);
            auto run = module ? reinterpret_cast<Run>(GetProcAddress(module, "WKW10MTestSourceState")) : nullptr;
            auto path = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\source-state-result.txt";
            auto result = run ? run(reinterpret_cast<IInspectable*>(m_view), reinterpret_cast<IInspectable*>(m_title), reinterpret_cast<IInspectable*>(m_back), path->Data()) : E_NOINTERFACE;
#if defined(TEST_INITIALIZATION_ACTIONS)
            // Async acceptance runs on the same actual XAML owner after the state regression.
            using RunInitialization = HRESULT(__stdcall*)(const wchar_t*);
            auto initialize = module ? reinterpret_cast<RunInitialization>(GetProcAddress(module, "WKW10MTestInitialization")) : nullptr;
            auto initializationPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\initialization-result.txt";
            auto initialized = initialize ? initialize(initializationPath->Data()) : E_NOINTERFACE;
            m_status->Text = SUCCEEDED(result) && SUCCEEDED(initialized) ? L"INITIALIZATION / STATE PASS " TEST_PACKAGE_VERSION_W : L"INITIALIZATION / STATE FAIL " TEST_PACKAGE_VERSION_W;
#else
            m_status->Text = SUCCEEDED(result) ? L"SOURCE / STATE PASS " TEST_PACKAGE_VERSION_W : L"SOURCE / STATE FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_EVENT_CALLBACKS)
            // Frozen-table acceptance uses the same owner and ordinary public event objects.
            using RunEvents = HRESULT(__stdcall*)(const wchar_t*);
            auto events = module ? reinterpret_cast<RunEvents>(GetProcAddress(module, "WKW10MTestEventCallbacks")) : nullptr;
            auto eventPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\event-callback-result.txt";
            auto eventResult = events ? events(eventPath->Data()) : E_NOINTERFACE;
            auto allPassed = SUCCEEDED(result) && SUCCEEDED(eventResult);
#if defined(TEST_INITIALIZATION_ACTIONS)
            allPassed = allPassed && SUCCEEDED(initialized);
#endif
            m_status->Text = allPassed ? L"EVENTS / INITIALIZATION / STATE PASS " TEST_PACKAGE_VERSION_W : L"EVENTS / INITIALIZATION / STATE FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_PUBLIC_COMMANDS)
            // Public command acceptance calls only the bound frozen page table.
            using RunCommands = HRESULT(__stdcall*)(const wchar_t*);
            auto commands = module ? reinterpret_cast<RunCommands>(GetProcAddress(module, "WKW10MTestCommands")) : nullptr;
            auto commandPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\command-result.txt";
            auto commandResult = commands ? commands(commandPath->Data()) : E_NOINTERFACE;
            auto commandsPassed = SUCCEEDED(result) && SUCCEEDED(commandResult);
#if defined(TEST_INITIALIZATION_ACTIONS)
            commandsPassed = commandsPassed && SUCCEEDED(initialized);
#endif
#if defined(TEST_EVENT_CALLBACKS)
            commandsPassed = commandsPassed && SUCCEEDED(eventResult);
#endif
            m_status->Text = commandsPassed ? L"COMMANDS / REGRESSIONS PASS " TEST_PACKAGE_VERSION_W : L"COMMANDS / REGRESSIONS FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_CONTROL_LIFECYCLE)
            // Ordinary lifetime/thread acceptance uses the same real XAML owner.
            using RunLifecycle = HRESULT(__stdcall*)(const wchar_t*);
            auto lifecycle = module ? reinterpret_cast<RunLifecycle>(GetProcAddress(module, "WKW10MTestLifecycle")) : nullptr;
            auto lifecyclePath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\lifecycle-result.txt";
            auto lifecycleResult = lifecycle ? lifecycle(lifecyclePath->Data()) : E_NOINTERFACE;
            auto lifecyclePassed = SUCCEEDED(result) && SUCCEEDED(lifecycleResult);
#if defined(TEST_INITIALIZATION_ACTIONS)
            lifecyclePassed = lifecyclePassed && SUCCEEDED(initialized);
#endif
#if defined(TEST_EVENT_CALLBACKS)
            lifecyclePassed = lifecyclePassed && SUCCEEDED(eventResult);
#endif
#if defined(TEST_PUBLIC_COMMANDS)
            lifecyclePassed = lifecyclePassed && SUCCEEDED(commandResult);
#endif
            m_status->Text = lifecyclePassed ? L"LIFECYCLE / REGRESSIONS PASS " TEST_PACKAGE_VERSION_W : L"LIFECYCLE / REGRESSIONS FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_NOTIFICATION_TRANSACTIONS)
            // Exact FIFO/notification trace acceptance runs against the frozen native table.
            using RunTransactions = HRESULT(__stdcall*)(const wchar_t*);
            auto transactions = module ? reinterpret_cast<RunTransactions>(GetProcAddress(module, "WKW10MTestTransactions")) : nullptr;
            auto transactionPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\transaction-result.txt";
            auto transactionResult = transactions ? transactions(transactionPath->Data()) : E_NOINTERFACE;
            auto transactionsPassed = SUCCEEDED(result) && SUCCEEDED(transactionResult);
#if defined(TEST_INITIALIZATION_ACTIONS)
            transactionsPassed = transactionsPassed && SUCCEEDED(initialized);
#endif
#if defined(TEST_EVENT_CALLBACKS)
            transactionsPassed = transactionsPassed && SUCCEEDED(eventResult);
#endif
#if defined(TEST_PUBLIC_COMMANDS)
            transactionsPassed = transactionsPassed && SUCCEEDED(commandResult);
#endif
#if defined(TEST_CONTROL_LIFECYCLE)
            transactionsPassed = transactionsPassed && SUCCEEDED(lifecycleResult);
#endif
            m_status->Text = transactionsPassed ? L"TRANSACTIONS / REGRESSIONS PASS " TEST_PACKAGE_VERSION_W : L"TRANSACTIONS / REGRESSIONS FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_CLIPBOARD_ROUTE)
            // Real clipboard routing acceptance runs on the same XAML owner thread.
            using RunClipboard = HRESULT(__stdcall*)(const wchar_t*);
            auto clipboard = module ? reinterpret_cast<RunClipboard>(GetProcAddress(module, "WKW10MTestClipboard")) : nullptr;
            auto clipboardPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\clipboard-result.txt";
            auto clipboardResult = clipboard ? clipboard(clipboardPath->Data()) : E_NOINTERFACE;
            auto clipboardPassed = SUCCEEDED(result) && SUCCEEDED(clipboardResult);
#if defined(TEST_INITIALIZATION_ACTIONS)
            clipboardPassed = clipboardPassed && SUCCEEDED(initialized);
#endif
#if defined(TEST_EVENT_CALLBACKS)
            clipboardPassed = clipboardPassed && SUCCEEDED(eventResult);
#endif
#if defined(TEST_PUBLIC_COMMANDS)
            clipboardPassed = clipboardPassed && SUCCEEDED(commandResult);
#endif
#if defined(TEST_CONTROL_LIFECYCLE)
            clipboardPassed = clipboardPassed && SUCCEEDED(lifecycleResult);
#endif
#if defined(TEST_NOTIFICATION_TRANSACTIONS)
            clipboardPassed = clipboardPassed && SUCCEEDED(transactionResult);
#endif
            m_status->Text = clipboardPassed ? L"CLIPBOARD / REGRESSIONS PASS " TEST_PACKAGE_VERSION_W : L"CLIPBOARD / REGRESSIONS FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_RUNTIME_OWNER)
            // Real runtime-owner acceptance runs on the same XAML owner thread.
            using RunOwner = HRESULT(__stdcall*)(const wchar_t*);
            auto owner = module ? reinterpret_cast<RunOwner>(GetProcAddress(module, "WKW10MTestOwner")) : nullptr;
            auto ownerPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\owner-result.txt";
            auto ownerResult = owner ? owner(ownerPath->Data()) : E_NOINTERFACE;
            auto ownerPassed = SUCCEEDED(result) && SUCCEEDED(ownerResult);
#if defined(TEST_INITIALIZATION_ACTIONS)
            ownerPassed = ownerPassed && SUCCEEDED(initialized);
#endif
#if defined(TEST_EVENT_CALLBACKS)
            ownerPassed = ownerPassed && SUCCEEDED(eventResult);
#endif
#if defined(TEST_PUBLIC_COMMANDS)
            ownerPassed = ownerPassed && SUCCEEDED(commandResult);
#endif
#if defined(TEST_CONTROL_LIFECYCLE)
            ownerPassed = ownerPassed && SUCCEEDED(lifecycleResult);
#endif
#if defined(TEST_NOTIFICATION_TRANSACTIONS)
            ownerPassed = ownerPassed && SUCCEEDED(transactionResult);
#endif
#if defined(TEST_CLIPBOARD_ROUTE)
            ownerPassed = ownerPassed && SUCCEEDED(clipboardResult);
#endif
            m_status->Text = ownerPassed ? L"OWNER / REGRESSIONS PASS " TEST_PACKAGE_VERSION_W : L"OWNER / REGRESSIONS FAIL " TEST_PACKAGE_VERSION_W;
#endif
#if defined(TEST_FRAME_SCHEDULER)
            // Asynchronous idle-resource acceptance must finish before the aggregate UI passes.
            using RunScheduler = Windows::Foundation::IAsyncAction^(__stdcall*)(const wchar_t*, const wchar_t*);
            auto scheduler = module ? reinterpret_cast<RunScheduler>(GetProcAddress(module, "WKW10MTestScheduler")) : nullptr;
            auto schedulerPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path + L"\\scheduler-result.txt";
            auto priorPassed = SUCCEEDED(result);
#if defined(TEST_INITIALIZATION_ACTIONS)
            priorPassed = priorPassed && SUCCEEDED(initialized);
#endif
#if defined(TEST_EVENT_CALLBACKS)
            priorPassed = priorPassed && SUCCEEDED(eventResult);
#endif
#if defined(TEST_PUBLIC_COMMANDS)
            priorPassed = priorPassed && SUCCEEDED(commandResult);
#endif
#if defined(TEST_CONTROL_LIFECYCLE)
            priorPassed = priorPassed && SUCCEEDED(lifecycleResult);
#endif
#if defined(TEST_NOTIFICATION_TRANSACTIONS)
            priorPassed = priorPassed && SUCCEEDED(transactionResult);
#endif
#if defined(TEST_CLIPBOARD_ROUTE)
            priorPassed = priorPassed && SUCCEEDED(clipboardResult);
#endif
#if defined(TEST_RUNTIME_OWNER)
            priorPassed = priorPassed && SUCCEEDED(ownerResult);
#endif
            m_status->Text = L"SCHEDULER / REGRESSIONS RUNNING " TEST_PACKAGE_VERSION_W;
            if (!scheduler) m_status->Text = L"SCHEDULER EXPORT MISSING " TEST_PACKAGE_VERSION_W;
            else {
                auto scheduled = scheduler(schedulerPath->Data(), TEST_SCHEDULER_BASE);
                scheduled->Completed = ref new Windows::Foundation::AsyncActionCompletedHandler(
                    [this, priorPassed](Windows::Foundation::IAsyncAction^ action, Windows::Foundation::AsyncStatus status) {
                        bool passed = priorPassed && status == Windows::Foundation::AsyncStatus::Completed;
                        try { action->GetResults(); } catch (...) { passed = false; }
                        m_status->Text = passed ? L"SCHEDULER / REGRESSIONS PASS " TEST_PACKAGE_VERSION_W : L"SCHEDULER / REGRESSIONS FAIL " TEST_PACKAGE_VERSION_W;
                    });
            }
#endif
        });
        Window::Current->Content = panel;
        Window::Current->Activate();
    }
private:
    // Embedded public control imported from compiler-generated WebKit.winmd.
    WebKit::WebKitWebView^ m_view;
    // Visible version-tagged run/result status.
    TextBlock^ m_status;
    // Loaded DocumentTitle binding target used by the native assertions.
    TextBlock^ m_title;
    // Loaded CanGoBack binding target used by the native assertions.
    Button^ m_back;
    // Loaded may fire repeatedly; execute this package's diagnostic once.
    bool m_ran { false };
};

} // namespace WebKitStateProbe

// Start the real UWP XAML application with the supported CX runtime entry path.
[Platform::MTAThread]
int main(Platform::Array<Platform::String^>^)
{
    Application::Start(ref new ApplicationInitializationCallback([](ApplicationInitializationCallbackParams^) {
        ref new WebKitStateProbe::App();
    }));
    return 0;
}
