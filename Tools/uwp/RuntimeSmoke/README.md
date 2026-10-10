# RuntimeSmoke sources moved (M10 task 8.5)

The RuntimeSmoke application host is now C# under
`Tools/uwp/diagnostics/RuntimeSmoke/`; the native payload
(`payload/RuntimeSmoke.cpp`, byte-identical) and the first-page diagnostics
driver (`payload/SmokeDiagnostics.cpp`, moved out of the retired
`SmokeHost.cpp`) live there too. See
`Tools/uwp/diagnostics/RuntimeSmoke/README.md` for the current build, stage,
package and device-run commands.

`stage-app.py` stays shared here on purpose: MiniBrowser and the engine
regression scripts also stage through it, so its path is unchanged.
