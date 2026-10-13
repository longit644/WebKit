# HostHtml production forwarding: Lumia v90

Date: 2026-10-12. Source checkpoint: `89adccf24b`, with local uncommitted 11.0 changes.

## Device and package

- Lumia 950 XL / RM-1085; OS `15254.603.armfre.feature2_rs3svc.200106-1633`.
- Device Portal: `https://192.168.1.52`.
- Package: `WebKitProbe.MiniBrowser_1.0.0.90_arm__2932e6rj4m41p`.
- Application: `WebKitProbe.MiniBrowser_2932e6rj4m41p!App`.
- Install accepted (HTTP 202); deployment completed with `Code=0`, `Success=true`;
  launch HTTP 200.
- Signed package: `%TEMP%\opencode\m11-minibrowser-hosthtml-candidate-v90.appx`.
- SHA-256: `89E1DBE55D4A3386E11FB244146BDBD3AAB0439EC922EAAC6E7AEC2DC48A6285`.
- Full build/payload/baseline provenance:
  `%TEMP%\opencode\m11-minibrowser-hosthtml-candidate-v90.identity.json`.
- Package audit: 28 ARMNT/AppContainer binaries; Skia present; recursive import,
  metadata and required asset audit passed. Signing and signature verification passed.

## Root cause and fix

The candidate stager resolved dependency DLLs from the preserved vcpkg install,
which still contains pre-repair runtime code. Earlier phone packages used repaired
build-tree overrides, so their runtime fixes were absent from these candidates.

v85's native trace distinguishes the error from a path or packaged-font problem:

```text
page-font-native-open result=0
page-font-native-open-extended result=0
page-font-native-size result=633604
page-font-native-map-error result=0
page-font-native-view-error result=0
page-font-native-read-bytes result=633604
page-font-freetype-open result=10
page-font-freetype-memory result=10
```

FreeType error 10 is `Array_Too_Large`. Disassembly of the installed DLL showed
the allocation overflow check calling an old `__rt_udiv` wrapper that forwards
MSVC's denominator-first operands without swapping them for compiler-rt. The
current `Tools/w10m/lib/arm-crtstart.lib` contains the corrected adapter. A
FreeType relink changed both face-open probes to success in v86. Relinking
Fontconfig too restored font registration and initialization in v87.

v87 then crashed during the HostHtml load. Its minidump (`m11-v87-crash.dmp`)
records exception `0xC0000005` in the phone's `msvcp140.dll+0x13A93`, called from
`icuuc78.dll`, with a null mutex implementation. The older phone runtime requires
`_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR`, already used by repaired ICU build-tree
artifacts and previously applied to ANGLE.

The durable staging fix requires an explicit `--dependency-baseline` plus
`--baseline-identity`. Every baseline DLL hash is verified before staging. The
baseline is preferred over the stale prefix through `stage-app.py --override`;
fresh engine binaries still win. v90 uses the preserved phone-tested dependency
closure in `%TEMP%\opencode\m10-engine-browser-stage-v33`, with hashes from its
matching `-identity.json`. Future native dependency builds receive the mutex
constructor setting from `arm32-w10m-env.ps1`.

| v90 binary | SHA-256 |
| --- | --- |
| WebCore.dll | `0BE720E80D229939F1E7F49AFF04B6F0A21E7451C48C48F2EB4D726C2089CC51` |
| JavaScriptCore.dll | `1619EC7D67542CDE89A287CA24DAFE4297AF8141E6D902080DC281E56FD1581A` |
| freetype.dll | `3BFF27A9F500927CCF87C20E1A3D2E235FAC72123F6FC3560F5B4B1F9407589F` |
| fontconfig-1.dll | `C15C71BA9B0F75D8A5083CE5F33F8838CD7E70BE08908BB0C15870C22C7FBE62` |
| icuuc78.dll | `7BC42C25BE3A1CDE607F8E674391C115C4C7DFA9C7D26247CF059926A5B1277C` |

The earlier `0x8007007B`/`0x80070002` reports were diagnostic stage mappings, not
proof of native filename/file-not-found errors. The readable raw trace supersedes
those path hypotheses.

## HostHtml result

The native adapter also incorrectly copied a substitute response's HTTP fields
into HostHtml events. The public control rejects HTTP status on local content.
The event builder now supplies those fields only for remote content.

Decisive v90 public-control report:

```text
package=1.0.0.90
initialized: result=S_OK
starting: id=1;kind=HostHtml;uri=about:blank
completed: id=1;kind=HostHtml;outcome=Completed;http=False;status=0
terminal: source=about:blank;back=False;forward=False;loading=False;title=;status=Host content Completed.
```

Native report shows `navigation-committed-id=1`, `document-bytes-parsed=374`,
`document-parsed-total=374`, `navigation-completed=0`, and successful owner service
entry/exit. The commit notification must be accepted for the control to accept
the terminal's advanced document generation. No host-demo history availability
was introduced.

Saved raw reports:

- `%TEMP%\opencode\m11-v90-hosthtml-candidate.txt` from LocalState.
- `%TEMP%\opencode\m11-v90-hosthtml-engine.txt` from `AC/Temp` on Mobile.
- `%TEMP%\opencode\m11-v90-{build,stage,install,state,launch}-1.log`.

## Remaining acceptance

This run proves initialization, engine commit/parse/finish, public Starting and
Completed delivery, local-content status shape, and Back/Forward unavailability.
Visible demo presentation is still pending: production presentation slots have
not been forwarded in this first slice. A screenshot request timed out after
the successful callback run. Remote navigation and TLS/consent qualification
follow the local presentation gate. Task 11.0 remains open; ordinary builds
continue to advertise capability mask zero.

## Focused regression checks

- Dependency-baseline guard: 4/4 tests, including stale replacement, missing ICU
  and unrecorded dependency rejection.
- Public-control contract: 4/4 tests.
- Input/PageController contract: 58/58 checks; both ARM boundary compilers pass.
- Ordinary provider negotiation: all checks passed, delivered mask `0x0`.
- Source documentation audit: 0 candidates; audit tests 9/9.
- `git diff --check`: passed.
