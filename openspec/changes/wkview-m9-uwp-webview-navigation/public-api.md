# Public API - WebView2-inspired WebKitWebView

> **Contract authority:** `control-contract.md` is the current v1 decision and
> supersedes conflicting API/version/event/lifecycle statements in this design
> sketch. Members marked deferred here are not v1 requirements until the
> follow-up is adopted.

## Reference and platform

Owner-selected interface reference: [Get started with WebView2 in WinUI](https://learn.microsoft.com/en-us/microsoft-edge/webview2/get-started/winui), especially steps 6-9 (XAML Source, address-bar navigation, navigation events and ExecuteScriptAsync).

This defines an intended M9 interface, not an implemented API. Runtime class: `WebKit.WebKitWebView`. XAML target: `Windows.UI.Xaml` for UWP/Windows 10 Mobile. It does not use WinUI 3, Windows App SDK, Edge/Chromium or Microsoft.Web.WebView2.Core types, and is not a binary/source-compatible WebView2 replacement.

## Application XAML

```xml
<Page
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:controls="using:WebKit">
    <Grid>
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto" />
            <RowDefinition Height="*" />
        </Grid.RowDefinitions>
        <Grid.ColumnDefinitions>
            <ColumnDefinition Width="*" />
            <ColumnDefinition Width="Auto" />
        </Grid.ColumnDefinitions>
        <TextBox x:Name="AddressBar" Grid.Row="0" Grid.Column="0" />
        <Button x:Name="GoButton" Grid.Row="0" Grid.Column="1" Content="Go" />
        <controls:WebKitWebView x:Name="MyWebView"
            Grid.Row="1" Grid.ColumnSpan="2"
            Source="https://www.microsoft.com"
            HorizontalAlignment="Stretch"
            VerticalAlignment="Stretch" />
    </Grid>
</Page>
```

The application sets `MyWebView.Source` to the entered absolute URI. It subscribes to control events to update its toolbar. It never downloads or injects remote HTML/resources to implement navigation. The XML alias `controls` is chosen by the application; the public type name remains WebKitWebView. V1 support and deferred members are defined by `control-contract.md`.

## Properties

| Member | Intended contract |
| --- | --- |
| Source | URI dependency property; writable; requests navigation, then reflects the effective committed URI without recursively navigating |
| CanGoBack / CanGoForward | Read-only observable booleans from engine history |
| IsLoading | Read-only observable main-navigation state |
| DocumentTitle | Read-only observable title of the current document |
| Session/profile configuration | V1: independent ephemeral session per control; explicit sharing/persistence deferred |
| Standard XAML properties | Name, grid placement, alignment, dimensions, visibility and ordinary layout integration |

## Methods

| Member | Intended contract |
| --- | --- |
| EnsureInitializedAsync | Awaitable initialization; lazy Source-based initialization also works; concurrent calls share an attempt |
| Navigate(uri) | Deferred as redundant in v1; set Source to navigate |
| NavigateToString(html) | V1 isolated app-supplied document, effective Source `about:blank`; opaque origin, no base-URI argument |
| GoBack / GoForward | Move through available engine history; no-op when unavailable |
| Reload | Reload the current entry without adding a duplicate entry |
| Stop | Cancel active document/resource loading and report the navigation's cancellation outcome |
| ExecuteScriptAsync(script) | Deferred from v1; if adopted later, uses JSON result semantics and document-generation cancellation |
| Close | Idempotent teardown/cancellation; the closed instance cannot navigate or execute scripts again |

All members use native WinRT/ABI contracts with projected language support, not an assumption that the ARM build supports C++ exception handling. Wrong-thread calls have an explicit thread-access error. Script execution does not change the document origin or weaken security rules; generation/lifetime checks prevent results from being attributed to a replacement document.

## Events and state sequence

| Event | Intended information |
| --- | --- |
| NavigationStarting | URI, navigation ID, redirect/user-initiation information, Cancel |
| SourceChanged | Current effective URI and whether this is a new document |
| ContentLoading | Deferred from v1 |
| HistoryChanged | Updated Back/Forward availability |
| NavigationCompleted | Navigation ID, success/cancel/error category, available HTTP status |
| DocumentTitleChanged | Current title |
| NewWindowRequested | Deferred from v1 until request/deferral semantics are designed |
| DownloadRequested | Deferred from v1 until request/deferral semantics are designed |
| PermissionRequested | Deferred from v1 until request/deferral semantics are designed |

A normal navigation starts with NavigationStarting and emits exactly one NavigationCompleted terminal event whose outcome is Completed, Failed or Cancelled. Redirect hops raise NavigationStarting with the same navigation ID and redirect information so host policy can evaluate each target. Commit updates Source and history state; DocumentTitleChanged follows actual document changes rather than a fixed ordering. Same-document Source/history changes do not pretend to be a new document load. HTTP error documents retain their HTTP status and are distinct from transport failures. Events are on the owning UI dispatcher and callback reentrancy must be covered by tests.

The tutorial's HTTPS-only NavigationStarting example is application policy, not a reason to hard-code a different HTTP policy into the engine or to disable TLS verification. Native UWP applications can display their own policy/error UI. Execution of `alert(...)` additionally depends on a supported script-dialog host contract; the tutorial's alert example is not proof of that feature on our implementation.

## Deliberate differences from WebView2

- No CoreWebView2 property, Edge runtime installation or Microsoft-named event-args classes. WebKit-specific event args and session objects live in our namespace.
- EnsureInitializedAsync is the explicit initialization method; it serves the comparable embedding need without suggesting CoreWebView2 exists.
- NavigationStarting/event names and Source/layout usage are familiar; exact supported semantics are governed by the M9 specs and this document.
- Advanced host objects, developer tools, arbitrary request interception and unrestricted process/runtime settings are not added merely because WebView2 exposes them.
- Full declarative/runtime usability is accepted only after activation, metadata, binding, multi-instance and phone tests pass.
