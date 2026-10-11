#Requires -Version 5.1
<#
.SYNOPSIS
  CX-aware W10M source documentation/format audit entry point.

.DESCRIPTION
  Forwards to Tools/w10m/audit-source-docs.py from the repository root, so the
  audit runs against the implemented M10 placement regardless of the caller's
  working directory. Old direct invocations (python Tools/w10m/audit-source-docs.py)
  keep working; this wrapper only fixes the working directory and interpreter lookup.

.EXAMPLE
  .\Tools\w10m\audit-source-docs.ps1 --check Source\WebKit\Shared\w10m
#>
[CmdletBinding()]
param(
  [Parameter(ValueFromRemainingArguments = $true)]
  [string[]]$AuditArgs
)
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$script = Join-Path $repoRoot 'Tools\w10m\audit-source-docs.py'
Push-Location -LiteralPath $repoRoot
try {
  & python $script @AuditArgs
  exit $LASTEXITCODE
} finally {
  Pop-Location
}
