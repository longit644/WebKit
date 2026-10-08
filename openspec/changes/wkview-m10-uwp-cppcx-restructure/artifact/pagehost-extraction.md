# M10 4.2 - PageHostUWP and client extraction with preserved baseline

PageUWP.cpp split into its planned files with no behavioral change. This
records what moved, how preservation was proven, and what is deliberately not
claimed.

## Delivered

- `Source/WebCore/platform/uwp/PageHostUWP.h` - the `PageHostUWP` struct moved
  verbatim, including its fixture-suite fields, which the suite reads.
- `Source/WebCore/platform/uwp/StorageSessionProviderUWP.h/.cpp` - session
  isolation kept under its existing name; the `storageSessionUWP` lookup used
  by the curl adapter is unchanged.
- `Source/WebCore/platform/uwp/BackForwardClientUWP.h/.cpp` - history kept
  under its existing name, including the 100-entry cap and branch truncation.
- `Source/WebCore/platform/uwp/ChromeClientUWP.h/.cpp` - the migration map's
  planned name for `PageChromeUWP`; the rename is safe because the class was
  file-local.
- `Source/WebCore/platform/uwp/FrameLoaderClientUWP.h/.cpp` - the migration
  map's planned name for `PageFrameLoaderUWP`; the fixture free functions
  driven only by this client move with it and stay file-local, since no export
  calls them.
- `Source/WebCore/PlatformWin.cmake` - the four new sources join the `WebCore`
  target; no header list changes, matching the `PageUWP.h` precedent.
- `PageUWP.cpp` keeps the exports, the handle cast, size validation and the
  fixture-free remainder; its two client construction sites use the new names.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- All five affected objects compile in the fresh ARM32 target, and the full
  `WebCore` target links: `bin/WebCore.dll` is `IMAGE_FILE_MACHINE_ARMNT`,
  AppContainer, with all 24 legacy exports plus `WKUWPGetEngineAPI`.
- Move fidelity is mechanical, not trusted: the fixture block (507 lines) and
  the host struct are byte-identical to the committed parent modulo
  whitespace, every history and chrome method body is likewise identical, and
  no `PageChromeUWP`/`PageFrameLoaderUWP` identifier remains outside two
  historical notes.
- The new TU needed three `*Inlines.h` includes the old TU already had
  (`LocalFrame`, `FrameDestructionObserver`, computed-style getters); without
  them the same calls that linked before become undefined symbols, which the
  link proved immediately.
- The task 3.6 legacy-surface gate was evolved, not waived: its declaration,
  wrapper and host checks still run and pass, while the two engine
  implementation files it pinned now evolve by design under tasks 4.1-4.2.

Final DLL SHA-256:

```text
4.2 WebCore.dll: F3D454F9D0A6D6C84FC6533B5670A726D7DC4D281881C24CB69EC9C7126F301F
```

## Not claimed

- No behavioral change: creation, history, policy, rendering and fixture
  paths are the same code in new files. No new engine behaviour was added.
- No device evidence. The ARM32 module is built, inspected and export-checked;
  phone behaviour of page creation/history/rendering is tasks 8.x/9.x with a
  matching package.
- No fixture-suite disposition: the suite moves with its driver and is removed
  wholesale in task 9.7.
