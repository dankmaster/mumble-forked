[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

throw @'
The one-phase input-enhancement release rehearsal has been retired because it
could consume observer/VM receipts before the signed candidate existed.

Run prepare-input-enhancement-release-rehearsal.ps1 first. It emits the
immutable rehearsal-challenge.json and exits without creating a draft. Run
independent kill-switch and updater-VM observation against that challenge,
then run finalize-input-enhancement-release-rehearsal.ps1 exactly once.
'@
