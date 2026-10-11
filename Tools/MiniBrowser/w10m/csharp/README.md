# Windows 10 Mobile MiniBrowser (M10 tasks 8.1 and 8.2)

C#/XAML W10M application targeting Windows 10 Mobile, hosting the public `WebKit.WebKitWebView` control from compiled component metadata (`WebKit.winmd`). No `Source/WebKit/UIProcess/API/w10m/` source is compiled into this app; the WinMD plus its matching implementation DLL is the only contract. `BrowserViewModel` supplies address, title, history, loading, status, notification-demo and menu state. A phone Edge-inspired 48-DIP bottom bar contains a loading-swapped Reload/Stop control, a stretch address field, and the three-dot action toggle. The compact address text is shifted left 3 DIP to balance the refresh-control region; Enter submits the address. System Back comes from `SystemNavigationManager.BackRequested`. There is no extra Go button or tab switcher. Startup keeps null Source and an empty status; no permanent Ready footer or test navigation runs.

The W10M app uses `MvvmLightLibs` 5.4.1.1 (`ViewModelBase`, `RelayCommand`, and `SimpleIoc`). `MiniBrowserServiceLocator` registers app- and page-scoped dependencies; `BrowserService` adapts the XAML-created WebKit control and owns its event subscriptions; `BrowserPolicy` remains the app-owned navigation/warning policy. `MainPage` coordinates window lifecycle, visible bounds, keyboard handling, and animations. `Views/Controls/AddressBarControl` and `BrowserAppBarControl` isolate the address and app-bar visuals in the same project/assembly. New C# declarations include XML documentation.

An embedding page can construct the public control through compiled XAML without assigning an initial source:

```xml
<Page
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:web="using:WebKit">
    <web:WebKitWebView x:Name="Browser" />
</Page>
```

Keep the page/control alive when it is temporarily unloaded or the application is suspended; `Close()` is terminal and belongs to the owner that is actually ending the control's lifetime. This shell revokes temporary page event subscriptions on `Unloaded` and does not call `Close()` there. Final app-shutdown/engine shared-lifetime behavior remains an open section-8 verification item.

The three-dot button reveals Edge's three-layer composition: the persistent address bar, one native AppBarButton Hello demonstration with a dismissible `1` notification badge, and a compact secondary menu. Tapping Hello clears the badge and raises a native Windows toast titled “Hello”; no custom status-footer row is shown. During loading, a 2-DIP accent progress indicator appears immediately above the address bar. The secondary list contains Forward and Local demo; Forward is disabled when history has no forward entry. Back is handled only through the Windows navigation-bar event. Menu rows use 40-DIP touch targets, 14-DIP text, 16-DIP icons, neutral gray hover/pressed feedback, native pointer-down/up animation, and a 1-DIP gray top divider. The expanded surface spans the visible root width in portrait and its right half in landscape. The primary action strip matches the native AppBarButton height of 56 DIP. Native AppBar resources provide system-theme colors; disabled buttons use a transparent surface with muted foreground. Opening the panel applies a half-opacity black scrim to the document. Tall menus scroll inside the remaining space above the fixed action strip. The view owns animation and geometry; the ViewModel owns open/closed state and commands. Tapping outside, pressing system Back/Escape, toggling the dots again, or selecting an available action dismisses the panel. Interrupted animation completions cannot collapse a newer panel state.

The latest compact-menu refinement sets Forward and Local demo menu labels to 14 DIP, with 40-DIP rows and 16-DIP icons. The Hello action retains native AppBarButton sizing.

Carry from task 6.3: `App` requests `ApplicationViewBoundsMode.UseCoreWindow` before creating/navigating the Frame. `MainPage` alone applies `VisibleBoundsHelper.ComputeMargins` to `ShellRoot`; combining `UseVisible` with these margins caused repeated safe-area insets in the earlier shell. The page refreshes margins after Loaded, SizeChanged, and VisibleBoundsChanged, and revokes its external subscriptions on Unloaded. Application and chrome inherit Windows light/dark settings through ThemeResource brushes. Web page colors remain document-owned.

```powershell
# Full gate: fresh component metadata + ARM Release .NET Native + sign + verify
  .\Tools\MiniBrowser\w10m\csharp\build-arm-w10m-minibrowser.ps1 `
  -OutDir "$env:TEMP\opencode\m10-w10m-minibrowser-v48" -Version 0.0.0.48 -LayoutDiagnostics
# Host-runnable pure-math check only
  .\Tools\MiniBrowser\w10m\csharp\test-visible-bounds-math.ps1
```

Targets 10.0.19041.0 / MinVersion 10.0.15063.0, Publisher `CN=WebKitWebView`,
`Microsoft.NETCore.UniversalWindowsPlatform` 6.0.15, ARM Release through
.NET Native, Community 2022 MSBuild. Package `WebKitProbe.MiniBrowser`
1.0.0.22; app ID `WebKitProbe.MiniBrowser_2932e6rj4m41p!App` once installed
(family name varies by signing key hash — confirm via install output).
Suspension/Close lifetime belongs to task 8.8.

## Device VisibleBounds run (owner)

Install with Device Portal, require deployment `Code=0`/`Success=true`, then
launch `WebKitProbe.MiniBrowser_2932e6rj4m41p!App` using the installed package
full name. `-LayoutDiagnostics` opts into geometry-only LocalState reports; ordinary builds omit the report code. Owner photographs still establish visual acceptance.

```powershell
python Tools/w10m/device-portal.py --url https://<portal> --insecure install `
  "$env:TEMP\opencode\m10-minibrowser-relocated-v22\MiniBrowserW10M-arm.appx"
python Tools/w10m/device-portal.py --url https://<portal> --insecure get /api/app/packagemanager/state
python Tools/w10m/device-portal.py --url https://<portal> --insecure launch `
  'WebKitProbe.MiniBrowser_2932e6rj4m41p!App' `
  'WebKitProbe.MiniBrowser_1.0.0.22_arm__2932e6rj4m41p'
```

Confirm the browser fills the area above the bottom address bar, with no repeated blank strip above/below chrome and no custom status footer. Rotate portrait → landscape → portrait, keep the system navigation bar visible, and confirm there is no content beneath it. Open the app-bar panel in both orientations: full width in portrait, half width aligned right in landscape, Forward/Local demo rows flush above the address bar, and the single Hello action immediately above it. Check 48-DIP menu rows, 14-DIP text, gray hover/pressed feedback, Forward disabled state, badge dismissal and system toast, outside-tap/system Back dismissal, repeated toggles, keyboard Enter, and light/dark chrome. For address editing, confirm the divider between address and dots in compact mode, the full-width editor and dimmed document while focused, and draft restoration after outside-tap, Escape, or Back. Loading should show a 2-DIP accent progress line immediately above the address bar and replace Reload with Stop in the same left control slot. A `-LayoutDiagnostics` build records the package version and verifies root/chrome/progress/button geometry, plus three open-menu checks and two address-editing checks. The report records theme and keyboard occlusion, never address text or page URI. Reports retain at most twelve snapshots.

## Browser policy and certificate-warning run (M10 task 8.3)

`BrowserPolicy` is the app-owned HTTP(S) navigation and certificate-warning
policy, integrated with the `BrowserViewModel` commands and status bindings. Its
integration surface is:

```csharp
BrowserPolicy(WebKit.WebKitWebView view, string packagedBlockedTemplatePath,
    string overridePath = null, string packagedDemoTemplatePath = null)
event Action<string> StatusChanged;
event Action<bool> WarningConsentReady;
void Navigate(Uri uri);
void GoBack();
void GoForward();
void Reload();
void NavigateToString(string html);
void NavigateToDemo();
void ProceedWithCurrentWarning();
void Stop();
void ClearSource();
void Dispose();
```

The ViewModel routes address navigation, Back, Forward, Reload, Stop, Demo,
app-owned markup, and null Source through this policy. No-op history commands
also invalidate warning eligibility. The packaged template is
`Assets/blocked.html`; an optional readable LocalState override takes precedence.
Malformed or missing overrides fall back to the packaged template, then to the
built-in safe template if the package asset is unavailable. `Assets/demo.html`
loads as opaque-origin HostHtml without a remote base or history insertion.

On a device, verify the warning run as follows:

1. Navigate to an HTTPS endpoint with a deliberately untrusted/expired
   certificate and confirm status reports warning content loading.
2. Confirm `WarningConsentReady` remains false until the associated HostHtml
   proposal has a matching HostHtml `NavigationCompleted` with `Completed`.
3. Confirm a different navigation, generic HTML load, Stop, or null Source
   clears readiness. A HostHtml ID must never be passed as a certificate token.
4. After the matching warning completion, confirm readiness becomes true;
   invoke `ProceedWithCurrentWarning()` only from the app's explicit consent
   action and verify it retries the retained HTTPS target once.
5. Confirm a second Proceed, an unrelated token, or a failed/cancelled warning
   does not retry. Confirm ordinary certificate failure text/status does not
   itself grant consent.

Host-only verification (no WinRT control required):

```powershell
.\Tools\MiniBrowser\w10m\csharp\test-browser-policy.ps1
```

## Address editing and keyboard-safe layout (v1.0.0.7)

The compact bottom bar has a system-themed 1-DIP divider between the address field and the three-dot button. Focusing the address enters MVVM editing state: the bar is 44 DIP high, the editor spans the bar with 4-DIP side margins and 14-DIP text, and Reload, dots, and divider collapse. A neutral document scrim is shown. Enter submits through BrowserPolicy; outside tap, Escape, system Back, or input-pane dismissal cancels the draft and restores the latest committed address.

InputPane occlusion is folded into the visible-bounds margin without shrinking the Frame/Page. Keyboard and system navigation insets are combined by maximum, and an already keyboard-reduced VisibleBounds is not inset twice. The optional layout report now records keyboard occlusion and checks editing geometry/focus/control visibility while preserving the six base checks plus two checks per active menu/editing state. Reports do not contain address text or page URIs.

Verified ARM package build: `-OutDir "$env:TEMP\opencode\m10-minibrowser-edge-editing-v7" -Version 1.0.0.7 -LayoutDiagnostics`. The Lumia 950 XL installed/launched v7; all collected package-version-matched snapshots in `%TEMP%\opencode\m10-minibrowser-v7-layout.txt` pass, including compact mode, full-width focused editing, and docked keyboard positioning. The report proves measured geometry, not owner screenshot/theme/landscape visual acceptance.

The v9 visual refinement retains `AddressBox.VerticalContentAlignment="Center"` and adds a 4-DIP top-versus-bottom padding difference, shifting the text area down by 2 DIP to compensate for the phone's apparent high glyph placement. Optional layout diagnostics report the actual padding and assert the same optical offset in both compact and editing states.

The Windows 10 Mobile app now lives under `Tools/MiniBrowser/w10m/csharp/`; `MiniBrowserW10M.csproj` and `build-arm-w10m-minibrowser.ps1` identify the target explicitly. W10M remains the underlying Windows app model and API surface. Historical package 1.0.0.22 built and signed, installed and launched on Lumia 950 XL / Mobile 15254.603; its base layout report passes. The v20 open-menu report verifies the native AppBarButton at 68×56 DIP and menu docking. BrowserPolicy is 23/23 and visible-bounds math 124/124. The MVVM-refactored shell currently builds/signs as package 0.0.0.48; its post-refactor device verification is pending.
