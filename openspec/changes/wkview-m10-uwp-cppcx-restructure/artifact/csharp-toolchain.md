# M10 application language decision and C# toolchain gate

## Decision

Application hosts and shells are C#: the owner controls the application layer
in C#/XAML over the C++/CX component. Engine, component, OS service
projections and native diagnostic payloads stay native and C++/CX; the
component WinMD is the only contract between them. Diagnostic behavior gates
in sections 5-7 and 9 keep proving the component and engine, never the shell
language. Tasks §8, the `uwp-application-hosting` spec and the migration map
carry this decision; proposal/design history is left frozen.

## Toolchain gate (proven before any §8 implementation)

Community 2022 17.14.41 plus, via Modify, the **Universal Windows Platform
tools** individual component with **.NET Native** (the UWP workload tile no
longer exists in the installer; WinUI does not target the phone). The
existing 19041 SDK install is untouched — newer SDKs dropped ARM32 libs.

Proof package `WebKitProbe.CSharpProof_1.0.0.1_arm__2932e6rj4m41p`
(blank C#/XAML app, Target 19041, MinVersion 15063, framework 6.0.15 restored
from nuget.org, ARM Release through .NET Native, signed with the existing
development certificate): installed Code=0/Success=true, launched HTTP 200 on
Lumia 950 XL / Mobile 15254.603, `LocalState/csharp-proof.txt` reports
**version=1.0.0.1, result=PASS, checks=4, failures=0** (owning dispatcher
thread, current window, compiled-XAML page, LocalState round trip).

SHA256: `B57D1EA99A25C4E3B3825020D3D24712C64366687431833A167DD6B5A50DF535`

## Discipline for every C# app in M10

- Target 10.0.19041.0, MinVersion 10.0.15063.0 (anything higher refuses the
  phone), Publisher matching the development certificate.
- `Microsoft.NETCore.UniversalWindowsPlatform` 6.0.x or older; the locally
  cached 6.2.10 cannot install on 15254.
- ARM Release through .NET Native for device runs; packaging comes from the
  MSBuild UWP targets while the existing signtool certificate and Device
  Portal install/launch/fetch flow applies unchanged.
- C# conventions for C# code (XML doc comments on types/members), XAML
  conventions for markup, matching the repository documentation audits.
