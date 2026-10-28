$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $repo "build-dependencies.json") -Raw | ConvertFrom-Json

# Builds inside Valve's SteamRT3 "sniper" SDK image, so the plugin runs on the same glibc as CS2.
if ($null -ne (Get-Command docker.exe -ErrorAction SilentlyContinue)) {
    $mount = $repo.Replace("\", "/")
} elseif ($null -ne (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    $mount = (& wsl.exe -d Ubuntu -- wslpath -a $repo.Replace("\", "/")).Trim()
} else {
    throw "Docker (or WSL with Docker) is required for the Linux build."
}
$arguments = @("run", "--rm", "--volume", "${mount}:/work", "--workdir", "/work", $manifest.steamrt3_image,
    "bash", "/work/scripts/build-linux.sh")
if ($null -ne (Get-Command docker.exe -ErrorAction SilentlyContinue)) {
    & docker.exe @arguments
} else {
    & wsl.exe -d Ubuntu -- docker @arguments
}
if ($LASTEXITCODE -ne 0) {
    throw "Linux build failed with exit code $LASTEXITCODE."
}
