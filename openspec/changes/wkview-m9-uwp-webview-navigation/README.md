# M9 - reusable UWP WebKitWebView and real web loading

## Status

**Superseded by [M10](../wkview-m10-uwp-cppcx-restructure/README.md).** Stop adding
implementation work to this plan. Preserve its tasks, implementation and device
records as history; it is not completed or archived as accepted. M10's
`artifact/migration-map.md` transfers outstanding requirements and identifies reusable evidence.

The remaining text describes the historical M9 plan, not current architectural
authority. M10's contracts/tasks and Tools/uwp/DESIGN.md govern new implementation.

Baseline: commit `5e0f025405`, MiniBrowser v1.0.0.33, Skia Ganesh and verified HTTPS/main-HTML loading on Lumia 950 XL. Wikipedia/GitHub currently render only the HTML available through the shell shortcut. Their external resources and normal navigation are the work covered here.

## Architecture contract

Application XAML embeds `<web:WebKitWebView />`; the application provides UX/UI and explicit policy decisions. The control owns navigation, subresources, web execution, rendering, input integration, page/session lifecycle and public events. Use WebCore's existing semantics and curl/OpenSSL integration; do not implement per-site scraping or manually inject CSS/scripts/images. MiniBrowser always uses Skia.

## Artifacts

- `proposal.md`: motivation, scope, capabilities and affected systems.
- `design.md`: ownership, threading, loader/client/session architecture, WinRT/XAML packaging, migration and risks.
- `control-contract.md`: authoritative v1 API, event, lifecycle, TLS-consent and source-placement decisions.
- `fixture-extraction-plan.md`: assertion-by-assertion replacement plan before removing the PageUWP device runner.
- `public-api.md`: owner-selected WebView2-inspired API reference and example markup; `control-contract.md` overrides conflicting sketch details.
- `specs/uwp-webview-control/spec.md`: public reusable control contract.
- `specs/uwp-web-navigation/spec.md`: correct document navigation/history/outcomes.
- `specs/uwp-web-resource-loading/spec.md`: asynchronous resources, sessions, security and device acceptance.
- `tasks.md`: dependency-ordered implementation steps with completion checks.

Before beginning XAML-control implementation, resolve the gates in
`control-contract.md` and map replacement coverage in
`fixture-extraction-plan.md`. The old `design-winrt-component.md` is prototype
history, not normative implementation guidance.

## Commands

```powershell
openspec status --change wkview-m9-uwp-webview-navigation
openspec validate wkview-m9-uwp-webview-navigation --strict
openspec instructions apply --change wkview-m9-uwp-webview-navigation
```

For new work, use M10's tasks and artifact/implementation-handoff.md. The commands above
inspect historical M9 artifacts only. Spec validation does not mean implementation
or device acceptance; retain the accepted Skia/TLS evidence with its package identity.
