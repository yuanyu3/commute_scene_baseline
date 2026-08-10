# Sync SceneEngine from sa_cpp into sa_service (vendored OHOS copy)
$ErrorActionPreference = "Stop"
$root = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
if (-not (Test-Path "$root\sa_cpp\include\commute_sa\scene_engine.h")) {
  $root = "D:\huawei\commute_scene_baseline"
}
$srcInc = Join-Path $root "sa_cpp\include\commute_sa"
$dstInc = Join-Path $root "sa_service\services\include\commute_sa"
$srcSrc = Join-Path $root "sa_cpp\src"
$dstSrc = Join-Path $root "sa_service\services\src\commute_sa"
New-Item -ItemType Directory -Force -Path $dstInc, $dstSrc | Out-Null
$headers = @("geo.h","anchors.h","theta.h","scene_engine.h","baseline_runtime.h","radio_evidence.h","pdr_evidence.h","anchor_reestimate.h","personalization.h","product_store.h","evidence_query.h","action_ops.h","theta_eval.h","crs.h","types.h")
$sources = @("anchors.cpp","theta.cpp","scene_engine.cpp","baseline_runtime.cpp","radio_evidence.cpp","pdr_evidence.cpp","anchor_reestimate.cpp","personalization.cpp","product_store.cpp","evidence_query.cpp","action_ops.cpp","theta_eval.cpp","crs.cpp")
foreach ($h in $headers) { Copy-Item (Join-Path $srcInc $h) $dstInc -Force }
foreach ($s in $sources) { Copy-Item (Join-Path $srcSrc $s) $dstSrc -Force }
Write-Host "Synced SceneEngine -> sa_service/services/{include,src}/commute_sa"
