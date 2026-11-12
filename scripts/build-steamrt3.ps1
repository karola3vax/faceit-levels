$ErrorActionPreference = "Stop"
$repo = Split-Path -Parent $PSScriptRoot
$manifest = Get-Content -LiteralPath (Join-Path $repo "build-dependencies.json") -Raw | ConvertFrom-Json

# Builds inside Valve's SteamRT3 "sniper" SDK, so the plugin runs on the same glibc as CS2.
# Valve's Steam Runtime 3 "sniper" SDK (the same files as the steamrt3_image Docker image),
# installed as the cs2-build WSL distro by 07-Tools/cs2-build-wsl/setup.ps1 in the workspace.
# kit-run.sh gives every build a fresh, throwaway copy of the kit, like "docker run --rm".
if ($null -eq (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "WSL 2 is required for the SteamRT3 build."
}
& wsl.exe -d cs2-build -u root --exec /opt/cs2kit/kit-run.sh --expect-build-id $manifest.steamrt3_build_id `
    --bind $repo.Replace("\", "/") /work `
    --workdir /work `
    -- bash /work/scripts/build-linux.sh
if ($LASTEXITCODE -ne 0) {
    throw "Linux build failed with exit code $LASTEXITCODE."
}
