# Builds AmpSim in Release and packages it for installation on another Windows machine, the
# counterpart of package.sh. Run it from a Developer PowerShell for Visual Studio, which puts MSVC
# and dumpbin on the PATH. Inno Setup 6 must be installed; GitHub's Windows runner image has it.
#
#     ./packaging/package-windows.ps1 [-BuildDir build-release]
#
# The installer lands in <build>/artefacts. It is not signed, so SmartScreen warns on first run;
# packaging/README.md says how to get past that and what signing would change.

param(
    # A caller that has already built Release, CI say, points this at that tree instead of paying
    # for a second full build.
    [string] $BuildDir = "build-release"
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = if ([System.IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $root $BuildDir }
$staging = Join-Path $build "package"
$out = Join-Path $build "artefacts"

# The same pattern package.sh and the release step use, so the three cannot disagree about which
# version this is.
$cmakeLists = Get-Content (Join-Path $root "CMakeLists.txt") -Raw
if ($cmakeLists -notmatch '(?m)^project\(AmpSim VERSION ([0-9.]+)') {
    throw "Could not read the version out of CMakeLists.txt"
}
$version = $Matches[1]
"==> AmpSim $version"

# Any native command that fails ends the script, the way set -e does in package.sh.
function Invoke-Checked {
    & $args[0] $args[1..($args.Count - 1)]
    if ($LASTEXITCODE -ne 0) { throw "$($args[0]) failed with exit code $LASTEXITCODE" }
}

# --- build -------------------------------------------------------------------------------------

Invoke-Checked cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DAMPSIM_BUILD_TESTS=OFF -DAMPSIM_COPY_PLUGIN=OFF | Out-Null
Invoke-Checked cmake --build $build --target AmpSim_VST3 AmpSim_Standalone | Out-Null

$artefacts = Join-Path $build "AmpSim_artefacts/Release"
$vst3 = Join-Path $artefacts "VST3/AmpSim.vst3"
$exe = Join-Path $artefacts "Standalone/AmpSim.exe"

# --- check -------------------------------------------------------------------------------------

# The C++ runtime is linked statically (CMakeLists.txt), so neither binary should import it. One
# that does would load on the machine that built it and fail on a machine without the Visual C++
# redistributable, which is not something any test here would see.
if (-not (Get-Command dumpbin -ErrorAction SilentlyContinue)) {
    throw "dumpbin is not on the PATH: run this from a Developer PowerShell for Visual Studio"
}
foreach ($binary in @($exe, (Join-Path $vst3 "Contents/x86_64-win/AmpSim.vst3"))) {
    $imports = & dumpbin /nologo /dependents $binary
    $runtime = $imports | Select-String -Pattern 'VCRUNTIME|MSVCP|ucrtbase|api-ms-win-crt'
    if ($runtime) {
        throw "$binary imports the DLL runtime, which the installer does not provide:`n$($runtime -join "`n")"
    }
    "==> $(Split-Path -Leaf $binary) links the C++ runtime statically"
}

# --- stage -------------------------------------------------------------------------------------

Remove-Item -Recurse -Force $staging, $out -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force (Join-Path $staging "VST3"), (Join-Path $staging "Standalone"), $out | Out-Null
Copy-Item -Recurse $vst3 (Join-Path $staging "VST3")
Copy-Item $exe (Join-Path $staging "Standalone")

# --- package -----------------------------------------------------------------------------------

$iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue |
    Select-Object -ExpandProperty Source -First 1
if (-not $iscc) {
    $iscc = Join-Path ${env:ProgramFiles(x86)} "Inno Setup 6/ISCC.exe"
}
if (-not (Test-Path $iscc)) { throw "Inno Setup 6 (ISCC.exe) was not found" }

$defines = @(
    "/DAppVersion=$version",
    "/DRepoRoot=$root",
    "/DStageDir=$staging",
    "/DOutputDir=$out"
)

# JUCE generates the application icon from resources/icon-*.png; the installer wears the same one.
$icon = Join-Path $build "AmpSim_artefacts/JuceLibraryCode/icon.ico"
if (Test-Path $icon) { $defines += "/DIconFile=$icon" }

"==> building the installer"
Invoke-Checked $iscc /Q @defines (Join-Path $root "packaging/windows/AmpSim.iss")

Get-ChildItem $out | ForEach-Object { "==> $($_.FullName)" }
