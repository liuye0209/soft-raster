# Download tinyrenderer african_head assets via gh-proxy.com with curl + retry + size validation
# Runs from the repo root next to assets/ (assets already ship with the skill -- run only if they are missing).
# Usage:  powershell -NoProfile -ExecutionPolicy Bypass -File fetch-assets.ps1 [assetDir]   (default .\assets)
# NOTE: environment blocks raw.githubusercontent.com directly; gh-proxy.com is the working mirror in that case.
$ErrorActionPreference = 'Stop'
$dest = Join-Path $PSScriptRoot $(if ($args.Count -gt 0) { $args[0] } else { 'assets' })
New-Item -ItemType Directory -Force $dest | Out-Null
$prefix = "https://gh-proxy.com/https://raw.githubusercontent.com/ssloy/tinyrenderer/master/obj/african_head"
$files = @(
  @{n='african_head.obj';           s=194973},
  @{n='african_head_diffuse.tga';   s=2801531},
  @{n='african_head_nm.tga';        s=4005778},
  @{n='african_head_nm_tangent.tga';s=3004900},
  @{n='african_head_spec.tga';      s=1000611},
  @{n='african_head_SSS.jpg';       s=6792}
)
foreach ($f in $files) {
  $out = Join-Path $dest $f.n
  if ((Test-Path $out) -and ((Get-Item $out).Length -eq $f.s)) { "SKIP $($f.n) (ok)"; continue }
  $done = $false
  for ($i = 1; $i -le 8; $i++) {
    Remove-Item "$out.part" -ErrorAction SilentlyContinue
    & curl.exe -sL --max-time 60 -A "opencode-fetch/1.0" -o "$out.part" "$prefix/$($f.n)"
    if ($LASTEXITCODE -eq 0 -and (Test-Path "$out.part") -and ((Get-Item "$out.part").Length -eq $f.s)) {
      Move-Item "$out.part" $out -Force
      "GOT $($f.n) ($($f.s) bytes, attempt $i)"
      $done = $true
      break
    }
    "RETRY $($f.n) attempt $i : exit=$LASTEXITCODE size=$((Get-Item "$out.part" -ErrorAction SilentlyContinue).Length) want $($f.s)"
  }
  if (-not $done) { "FAIL $($f.n)" }
}
"---- final ----"
Get-ChildItem $dest | ForEach-Object { "{0}  {1}" -f $_.Name, $_.Length }