# Tasks — wkview-m4-webviewuwp

- [ ] Agent: scaffold `Source/WebKit/UIProcess/API/uwp/` trio
  (`WebKitWebViewUWP.h/.cpp`, `WebKitWebViewBaseUWP.h/.cpp`,
  `PageClientImplUWP.h/.cpp`) mirroring gtk twins' structure
- [ ] Agent: write `Tools/UWP/UWP-MAPPING.md` (GObject→WinRT,
  signals→events, HWND→CoreWindow/SwapChainPanel)
- [ ] Agent: verify host build unaffected (uwp/ not in any build list),
  commit, push
- [ ] Owner: ack; agent tags `wkview-m4-webviewuwp`
