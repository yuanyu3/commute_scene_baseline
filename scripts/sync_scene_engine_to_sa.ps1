# Sync the canonical commute_sa implementation into the vendored OHOS copy.
[CmdletBinding()]
param(
  [switch]$Check
)

$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$srcInc = Join-Path $root "sa_cpp\include\commute_sa"
$dstInc = Join-Path $root "sa_service\services\include\commute_sa"
$srcSrc = Join-Path $root "sa_cpp\src"
$dstSrc = Join-Path $root "sa_service\services\src\commute_sa"
$headers = @("geo.h","anchors.h","theta.h","leave_hsmm.h","scene_engine.h","baseline_runtime.h","radio_evidence.h","pdr_evidence.h","baro_evidence.h","anchor_reestimate.h","personalization.h","personalization_optimizer.h","evidence_strength_profile.h","personalization_policy.h","product_store.h","evidence_query.h","action_ops.h","theta_eval.h","context_template.h","crs.h","types.h")
$sources = @("anchors.cpp","theta.cpp","leave_hsmm.cpp","scene_engine.cpp","baseline_runtime.cpp","radio_evidence.cpp","pdr_evidence.cpp","baro_evidence.cpp","anchor_reestimate.cpp","personalization.cpp","personalization_optimizer.cpp","evidence_strength_profile.cpp","personalization_policy.cpp","product_store.cpp","evidence_query.cpp","action_ops.cpp","theta_eval.cpp","context_template.cpp","crs.cpp")
$headers += "context_engine.h"

if ($Check) {
  $drift = @()
  foreach ($h in $headers) {
    $source = Join-Path $srcInc $h
    $target = Join-Path $dstInc $h
    if (-not (Test-Path $target) -or (Get-FileHash $source).Hash -ne (Get-FileHash $target).Hash) {
      $drift += "include/commute_sa/$h"
    }
  }
  foreach ($s in $sources) {
    $source = Join-Path $srcSrc $s
    $target = Join-Path $dstSrc $s
    if (-not (Test-Path $target) -or (Get-FileHash $source).Hash -ne (Get-FileHash $target).Hash) {
      $drift += "src/commute_sa/$s"
    }
  }
  if ($drift.Count -gt 0) {
    Write-Error ("Vendored commute_sa copy is out of sync:`n - " + ($drift -join "`n - "))
  }
  Write-Host "commute_sa vendored copy is in sync"
  exit 0
}

New-Item -ItemType Directory -Force -Path $dstInc, $dstSrc | Out-Null
foreach ($h in $headers) { Copy-Item (Join-Path $srcInc $h) $dstInc -Force }
foreach ($s in $sources) { Copy-Item (Join-Path $srcSrc $s) $dstSrc -Force }
Write-Host "Synced canonical commute_sa -> sa_service/services/{include,src}/commute_sa"
