<#
.SYNOPSIS
    Unified Windows 10/11 build, test, and flash entry point for uni-usb-dongle.

.DESCRIPTION
    Supports environment diagnostics (doctor), native host CMake/MSVC builds (build),
    automated host test execution with JUnit XML and JSON reporting (test),
    validated firmware OTA/UART flashing with dry-run and explicit confirmation gates (flash),
    and cleanup of Windows host build artifacts (clean).

.EXAMPLE
    .\tools\windows.ps1 doctor
    .\tools\windows.ps1 build
    .\tools\windows.ps1 build -Config Release
    .\tools\windows.ps1 test
    .\tools\windows.ps1 test -Name dualsense
    .\tools\windows.ps1 flash -Device COM5 -Firmware firmware\build\build_out\uni_usb_dongle_bl616.xz.ota -DryRun
    .\tools\windows.ps1 clean
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet("doctor", "build", "test", "flash", "backup", "clean", "help")]
    [string]$Action = "help",

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",

    [ValidateSet("Host", "Firmware", "All")]
    [string]$Target = "Host",

    [ValidateSet("m62_12f", "bl616dk")]
    [string]$Board = "m62_12f",

    [string]$BuildDir = "build\windows",

    [ValidateSet("All", "C", "Python", "Web", "Hardware")]
    [string]$Suite = "All",

    [string]$Name = "",

    [string]$ReportDir = "",

    [Alias("Port")]
    [string]$Device = "",

    [Alias("Image")]
    [string]$Firmware = "",

    [ValidateSet("auto", "hid", "ota", "uart")]
    [string]$Method = "auto",

    [int]$BaudRate = 115200,

    [switch]$DryRun,
    [switch]$Probe,

    [Alias("Yes")]
    [switch]$ConfirmFlash,

    [switch]$IncludeHardware,

    [ValidateSet("table", "boot2", "full")]
    [string]$Region = "table",

    [string]$Output = "",

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Script:RepoRoot = Split-Path -Parent $PSScriptRoot
$Script:ConfigWasExplicit = $PSBoundParameters.ContainsKey("Config")
$Script:M62Slot0LimitBytes = 2097152

function Join-Paths {
    param(
        [Parameter(Mandatory = $true)][string]$Base,
        [Parameter(Mandatory = $true)][string[]]$Parts
    )
    $current = $Base
    foreach ($part in $Parts) {
        $current = [System.IO.Path]::Combine($current, $part)
    }
    return $current
}

function Resolve-RepoPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Script:RepoRoot $PathValue))
}

function Resolve-InputPath {
    param([Parameter(Mandatory = $true)][string]$PathValue)
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    $fromCwd = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $PathValue))
    if (Test-Path -LiteralPath $fromCwd) {
        return $fromCwd
    }
    return [System.IO.Path]::GetFullPath((Join-Path $Script:RepoRoot $PathValue))
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )
    $parent = Split-Path -Parent $Path
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Get-FileSha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-BinaryFormat {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return "missing"
    }
    $bytes = New-Object byte[] 4
    $stream = [System.IO.File]::OpenRead($Path)
    try {
        $read = $stream.Read($bytes, 0, 4)
    } finally {
        $stream.Dispose()
    }
    if ($read -ge 2 -and $bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) {
        return "pe"
    }
    if ($read -eq 4 -and $bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and $bytes[2] -eq 0x4C -and $bytes[3] -eq 0x46) {
        return "elf"
    }
    return "unknown"
}

function Invoke-LoggedCommand {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @(),
        [string]$WorkingDirectory = $Script:RepoRoot,
        [hashtable]$EnvOverrides = @{},
        [switch]$StreamOutput
    )
    $savedLocation = Get-Location
    $savedEnv = @{}
    foreach ($key in $EnvOverrides.Keys) {
        $savedEnv[$key] = [Environment]::GetEnvironmentVariable($key, "Process")
        [Environment]::SetEnvironmentVariable($key, [string]$EnvOverrides[$key], "Process")
    }

    $lines = New-Object System.Collections.Generic.List[string]
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $exitCode = 1
    try {
        Set-Location -LiteralPath $WorkingDirectory
        try {
            & {
                $ErrorActionPreference = "Continue"
                & $FilePath @Arguments 2>&1 | ForEach-Object {
                    $line = if ($_ -is [System.Management.Automation.ErrorRecord]) {
                        $_.ToString()
                    } else {
                        [string]$_
                    }
                    $lines.Add($line)
                    if ($StreamOutput) {
                        Write-Host $line
                    }
                }
            }
            $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { [int]$LASTEXITCODE }
        } catch {
            $errLine = $_.Exception.Message
            $lines.Add($errLine)
            if ($StreamOutput) {
                Write-Host $errLine
            }
            $exitCode = 1
        }
    } finally {
        $sw.Stop()
        Set-Location -LiteralPath $savedLocation
        foreach ($key in $savedEnv.Keys) {
            [Environment]::SetEnvironmentVariable($key, $savedEnv[$key], "Process")
        }
    }

    return [pscustomobject]@{
        ExitCode    = $exitCode
        DurationSec = [Math]::Round($sw.Elapsed.TotalSeconds, 4)
        Output      = ($lines -join [Environment]::NewLine)
        Lines       = $lines.ToArray()
    }
}

function Find-VisualStudioRoot {
    $vswhereCandidates = @(
        (Join-Paths ${env:ProgramFiles(x86)} @("Microsoft Visual Studio", "Installer", "vswhere.exe")),
        (Join-Paths $env:ProgramFiles @("Microsoft Visual Studio", "Installer", "vswhere.exe"))
    ) | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) }

    foreach ($vswhere in $vswhereCandidates) {
        $res = Invoke-LoggedCommand -FilePath $vswhere -Arguments @(
            "-latest", "-products", "*",
            "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property", "installationPath"
        )
        if ($res.ExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($res.Output)) {
            $candidate = ($res.Lines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1).Trim()
            if ($candidate -and (Test-Path -LiteralPath $candidate -PathType Container)) {
                return $candidate
            }
        }
    }

    foreach ($progRoot in @($env:ProgramFiles, ${env:ProgramFiles(x86)})) {
        if (-not $progRoot) { continue }
        $vsBase = Join-Path $progRoot "Microsoft Visual Studio"
        if (-not (Test-Path -LiteralPath $vsBase -PathType Container)) { continue }
        $vcvars = Get-ChildItem -Path $vsBase -Filter "vcvars64.bat" -Recurse -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($vcvars) {
            return Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $vcvars.FullName)))
        }
    }
    return $null
}

function Find-FirstExistingFile {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return $candidate
        }
    }
    return $null
}

function Find-CommandExe {
    param([Parameter(Mandatory = $true)][string]$Name)
    $cmd = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cmd -and $cmd.Source -and (Test-Path -LiteralPath $cmd.Source -PathType Leaf)) {
        return $cmd.Source
    }
    return $null
}

function Find-BouffaloSdkBase {
    $candidates = @()
    if ($env:BL_SDK_BASE) {
        $candidates += $env:BL_SDK_BASE
    }
    $candidates += (Join-Path (Split-Path -Parent $Script:RepoRoot) "bouffalo_sdk")
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate "project.build") -PathType Leaf)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    return $null
}

function Find-CMakeTools {
    param([string]$VsRoot, [string]$SdkBase)
    $driveRoot = [System.IO.Path]::GetPathRoot($Script:RepoRoot)

    $cmakeCandidates = @()
    $pathCmake = Find-CommandExe "cmake"
    if ($pathCmake) { $cmakeCandidates += $pathCmake }
    if ($VsRoot) {
        $cmakeCandidates += (Join-Paths $VsRoot @("Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "cmake.exe"))
    }
    $cmakeCandidates += (Join-Paths $env:ProgramFiles @("CMake", "bin", "cmake.exe"))
    if ($driveRoot) {
        $espCmake = Get-ChildItem -Path (Join-Paths $driveRoot @("Espressif", "tools", "cmake")) -Filter "cmake.exe" -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($espCmake) { $cmakeCandidates += $espCmake.FullName }
    }
    if ($SdkBase) {
        $cmakeCandidates += (Join-Paths $SdkBase @("tools", "cmake", "bin", "cmake.exe"))
    }
    $cmakeExe = Find-FirstExistingFile $cmakeCandidates

    $ctestCandidates = @()
    if ($cmakeExe) {
        $ctestCandidates += (Join-Path (Split-Path -Parent $cmakeExe) "ctest.exe")
    }
    $pathCtest = Find-CommandExe "ctest"
    if ($pathCtest) { $ctestCandidates += $pathCtest }
    if ($VsRoot) {
        $ctestCandidates += (Join-Paths $VsRoot @("Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "CMake", "bin", "ctest.exe"))
    }
    $ctestExe = Find-FirstExistingFile $ctestCandidates

    $ninjaCandidates = @()
    $pathNinja = Find-CommandExe "ninja"
    if ($pathNinja) { $ninjaCandidates += $pathNinja }
    if ($VsRoot) {
        $ninjaCandidates += (Join-Paths $VsRoot @("Common7", "IDE", "CommonExtensions", "Microsoft", "CMake", "Ninja", "ninja.exe"))
    }
    if ($driveRoot) {
        $espNinja = Get-ChildItem -Path (Join-Paths $driveRoot @("Espressif", "tools", "ninja")) -Filter "ninja.exe" -Recurse -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | Select-Object -First 1
        if ($espNinja) { $ninjaCandidates += $espNinja.FullName }
    }
    if ($SdkBase) {
        $ninjaCandidates += (Join-Paths $SdkBase @("tools", "ninja", "ninja.exe"))
    }
    $ninjaExe = Find-FirstExistingFile $ninjaCandidates

    return [pscustomobject]@{
        CMake = $cmakeExe
        CTest = $ctestExe
        Ninja = $ninjaExe
    }
}

function Find-HostCompilerInfo {
    param([string]$VsRoot)
    $clPath = Find-CommandExe "cl.exe"
    $gccPath = Find-CommandExe "gcc.exe"
    $clangPath = Find-CommandExe "clang.exe"
    $vcvars = $null
    if ($VsRoot) {
        $candidate = Join-Paths $VsRoot @("VC", "Auxiliary", "Build", "vcvars64.bat")
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $vcvars = $candidate
        }
    }
    $kind = $null
    $exe = $null
    if ($clPath) {
        $kind = "msvc"
        $exe = $clPath
    } elseif ($vcvars) {
        $kind = "msvc"
        $vcToolsDir = Join-Paths $VsRoot @("VC", "Tools", "MSVC")
        if (Test-Path -LiteralPath $vcToolsDir -PathType Container) {
            $clFound = Get-ChildItem -Path $vcToolsDir -Filter "cl.exe" -Recurse -ErrorAction SilentlyContinue |
                Where-Object { $_.FullName -match "Hostx64\\x64\\cl\.exe$" } |
                Sort-Object FullName -Descending | Select-Object -First 1
            if ($clFound) { $exe = $clFound.FullName }
        }
    } elseif ($gccPath) {
        $kind = "gcc"
        $exe = $gccPath
    } elseif ($clangPath) {
        $kind = "clang"
        $exe = $clangPath
    }

    return [pscustomobject]@{
        Kind     = $kind
        Compiler = $exe
        VcVars64 = $vcvars
        OnPath   = [bool]$clPath -or [bool]$gccPath -or [bool]$clangPath
    }
}

function Initialize-MsvcEnvironment {
    param([Parameter(Mandatory = $true)][psobject]$CompilerInfo)
    if ($CompilerInfo.Kind -ne "msvc") {
        return
    }
    if ((Find-CommandExe "cl.exe") -and $env:INCLUDE -and $env:LIB) {
        return
    }
    if (-not $CompilerInfo.VcVars64) {
        throw "MSVC vcvars64.bat was not found. Install Visual Studio Desktop development with C++."
    }
    $cmdLine = "`"$($CompilerInfo.VcVars64)`" >nul 2>&1 && set"
    $output = & cmd.exe /c $cmdLine
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to initialize MSVC x64 build environment via $($CompilerInfo.VcVars64)."
    }
    foreach ($line in $output) {
        $idx = $line.IndexOf("=")
        if ($idx -gt 0) {
            $name = $line.Substring(0, $idx)
            $val = $line.Substring($idx + 1)
            [Environment]::SetEnvironmentVariable($name, $val, "Process")
        }
    }
}

function Test-PythonCandidate {
    param([string]$Candidate)
    if ([string]::IsNullOrWhiteSpace($Candidate)) {
        return $null
    }
    try {
        if (-not (Test-Path -LiteralPath $Candidate -PathType Leaf -ErrorAction SilentlyContinue)) {
            return $null
        }
    } catch {
        return $null
    }
    $res = Invoke-LoggedCommand -FilePath $Candidate -Arguments @(
        "-c", "import sys, unittest; print(sys.version.split()[0])"
    )
    if ($res.ExitCode -ne 0) {
        return $null
    }
    $ver = ($res.Lines | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
    if (-not $ver) { return $null }
    return [pscustomobject]@{
        Executable = $Candidate
        Version    = $ver.Trim()
    }
}

function Find-PythonRuntime {
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($env:PYTHON_EXE) { $candidates.Add($env:PYTHON_EXE) }

    if ($env:LOCALAPPDATA) {
        $candidates.Add((Join-Paths $env:LOCALAPPDATA @("Python", "bin", "python.exe")))
        Get-Item -Path (Join-Paths $env:LOCALAPPDATA @("Python", "pythoncore-*", "python.exe")) -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object { $candidates.Add($_.FullName) }
        Get-Item -Path (Join-Paths $env:LOCALAPPDATA @("Programs", "Python", "Python*", "python.exe")) -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object { $candidates.Add($_.FullName) }
    }

    foreach ($cmdName in @("python", "python3")) {
        $exe = Find-CommandExe $cmdName
        if ($exe) { $candidates.Add($exe) }
    }

    $pyLauncher = Find-CommandExe "py"
    if ($pyLauncher) {
        $res = Invoke-LoggedCommand -FilePath $pyLauncher -Arguments @("-3", "-c", "import sys; print(sys.executable)")
        if ($res.ExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($res.Output)) {
            $candidates.Add($res.Output.Trim())
        }
    }

    foreach ($regRoot in @("HKCU:\Software\Python\PythonCore", "HKLM:\SOFTWARE\Python\PythonCore")) {
        try {
            if (Test-Path -LiteralPath $regRoot -ErrorAction SilentlyContinue) {
                Get-ChildItem -Path $regRoot -ErrorAction SilentlyContinue | ForEach-Object {
                    $installKey = Join-Path $_.PSPath "InstallPath"
                    if (Test-Path -LiteralPath $installKey -ErrorAction SilentlyContinue) {
                        $props = Get-ItemProperty -LiteralPath $installKey -ErrorAction SilentlyContinue
                        if ($props -and $props.ExecutablePath) {
                            $candidates.Add([string]$props.ExecutablePath)
                        } elseif ($props -and $props.'(default)') {
                            $candidates.Add((Join-Path ([string]$props.'(default)') "python.exe"))
                        }
                    }
                }
            }
        } catch {
        }
    }

    if ($env:ProgramFiles) {
        Get-Item -Path (Join-Path $env:ProgramFiles "Python*\python.exe") -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object { $candidates.Add($_.FullName) }
    }

    $driveRoot = [System.IO.Path]::GetPathRoot($Script:RepoRoot)
    if ($driveRoot) {
        foreach ($extraPattern in @(
            (Join-Paths $driveRoot @("Python*", "python.exe")),
            (Join-Paths $driveRoot @("Documents", "*", "python", "python.exe"))
        )) {
            Get-Item -Path $extraPattern -ErrorAction SilentlyContinue | ForEach-Object {
                $candidates.Add($_.FullName)
            }
        }
    }

    $seen = New-Object System.Collections.Generic.HashSet[string]([System.StringComparer]::OrdinalIgnoreCase)
    $pythonInfo = $null
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) { continue }
        $norm = $candidate.Trim()
        if (-not $seen.Add($norm)) { continue }
        $tested = Test-PythonCandidate $norm
        if ($tested) {
            $pythonInfo = $tested
            break
        }
    }

    if (-not $pythonInfo) {
        return $null
    }

    # Check pyserial availability natively, then via PYSERIAL_PATH or neighboring venv site-packages.
    $sitePaths = New-Object System.Collections.Generic.List[string]
    if ($env:PYSERIAL_PATH) { $sitePaths.Add($env:PYSERIAL_PATH) }
    if ($driveRoot) {
        Get-Item -Path (Join-Paths $driveRoot @("Espressif", "tools", "python", "*", "venv", "Lib", "site-packages")) -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending | ForEach-Object { $sitePaths.Add($_.FullName) }
    }

    $pySerialOk = $false
    $pySerialVersion = $null
    $pySerialExtraPath = $null

    $probeNative = Invoke-LoggedCommand -FilePath $pythonInfo.Executable -Arguments @(
        "-c", "import serial; print(getattr(serial, '__version__', 'installed'))"
    )
    if ($probeNative.ExitCode -eq 0) {
        $pySerialOk = $true
        $pySerialVersion = $probeNative.Output.Trim()
    } else {
        foreach ($sp in $sitePaths) {
            if (-not (Test-Path -LiteralPath (Join-Path $sp "serial") -PathType Container -ErrorAction SilentlyContinue)) { continue }
            $probeExtra = Invoke-LoggedCommand -FilePath $pythonInfo.Executable -Arguments @(
                "-c", "import sys; sys.path.insert(0, sys.argv[1]); import serial; print(getattr(serial, '__version__', 'installed'))",
                $sp
            )
            if ($probeExtra.ExitCode -eq 0) {
                $pySerialOk = $true
                $pySerialVersion = $probeExtra.Output.Trim()
                $pySerialExtraPath = $sp
                break
            }
        }
    }

    return [pscustomobject]@{
        Executable        = $pythonInfo.Executable
        Version           = $pythonInfo.Version
        PySerialAvailable = $pySerialOk
        PySerialVersion   = $pySerialVersion
        PySerialSitePath  = $pySerialExtraPath
    }
}

function Find-NodeRuntime {
    $candidates = New-Object System.Collections.Generic.List[object]
    if ($env:NODE_EXE) {
        $candidates.Add([pscustomobject]@{ Executable = $env:NODE_EXE; IsElectron = $false })
    }
    $pathNode = Find-CommandExe "node"
    if ($pathNode) {
        $candidates.Add([pscustomobject]@{ Executable = $pathNode; IsElectron = $false })
    }
    foreach ($stdPath in @(
        (Join-Paths $env:ProgramFiles @("nodejs", "node.exe")),
        (Join-Paths ${env:ProgramFiles(x86)} @("nodejs", "node.exe"))
    )) {
        $candidates.Add([pscustomobject]@{ Executable = $stdPath; IsElectron = $false })
    }
    foreach ($electronPath in @(
        (Join-Paths $env:LOCALAPPDATA @("Programs", "Microsoft VS Code", "Code.exe")),
        (Join-Paths $env:ProgramFiles @("Microsoft VS Code", "Code.exe")),
        (Join-Paths $env:LOCALAPPDATA @("Programs", "antigravity", "Antigravity.exe"))
    )) {
        $candidates.Add([pscustomobject]@{ Executable = $electronPath; IsElectron = $true })
    }

    foreach ($cand in $candidates) {
        if ([string]::IsNullOrWhiteSpace($cand.Executable) -or -not (Test-Path -LiteralPath $cand.Executable -PathType Leaf)) {
            continue
        }
        $envMap = if ($cand.IsElectron) { @{ ELECTRON_RUN_AS_NODE = "1" } } else { @{} }
        $res = Invoke-LoggedCommand -FilePath $cand.Executable -Arguments @("-v") -EnvOverrides $envMap
        if ($res.ExitCode -eq 0 -and $res.Output -match "v\d+\.\d+\.\d+") {
            $ver = $Matches[0]
            $major = [int]($ver.TrimStart("v").Split(".")[0])
            return [pscustomobject]@{
                Executable = $cand.Executable
                Version    = $ver
                Major      = $major
                IsElectron = $cand.IsElectron
                Env        = $envMap
            }
        }
    }
    return $null
}

function Find-FirmwareTools {
    param([string]$SdkBase)
    $parentDir = Split-Path -Parent $Script:RepoRoot

    $blFlashExe = if ($SdkBase) {
        Find-FirstExistingFile @((Join-Paths $SdkBase @("tools", "bflb_tools", "bouffalo_flash_cube", "BLFlashCommand.exe")))
    } else { $null }

    $postProcExe = if ($SdkBase) {
        Find-FirstExistingFile @((Join-Paths $SdkBase @("tools", "bflb_tools", "bflb_fw_post_proc", "bflb_fw_post_proc.exe")))
    } else { $null }

    $makeCandidates = @()
    $pathMake = Find-CommandExe "make"
    if ($pathMake) { $makeCandidates += $pathMake }
    $pathMingwMake = Find-CommandExe "mingw32-make"
    if ($pathMingwMake) { $makeCandidates += $pathMingwMake }
    if ($SdkBase) {
        $makeCandidates += (Join-Paths $SdkBase @("tools", "make", "make.exe"))
        $makeCandidates += (Join-Paths $SdkBase @("tools", "make", "mingw32-make.exe"))
    }
    $makeExe = Find-FirstExistingFile $makeCandidates

    $toolchainCandidates = New-Object System.Collections.Generic.List[string]
    if ($env:TOOLCHAIN_BIN) { $toolchainCandidates.Add($env:TOOLCHAIN_BIN) }
    $pathRiscvExe = Find-CommandExe "riscv64-unknown-elf-gcc.exe"
    if ($pathRiscvExe) { $toolchainCandidates.Add((Split-Path -Parent $pathRiscvExe)) }
    Get-Item -Path (Join-Paths $parentDir @("Xuantie-*-mingw-*", "bin")) -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | ForEach-Object { $toolchainCandidates.Add($_.FullName) }
    Get-Item -Path (Join-Paths $parentDir @("Xuantie-*", "bin")) -ErrorAction SilentlyContinue |
        Sort-Object FullName -Descending | ForEach-Object { $toolchainCandidates.Add($_.FullName) }

    $toolchainBin = $null
    $toolchainFormat = "missing"
    foreach ($dir in $toolchainCandidates) {
        if (-not $dir -or -not (Test-Path -LiteralPath $dir -PathType Container)) { continue }
        $winGcc = Join-Path $dir "riscv64-unknown-elf-gcc.exe"
        $nixGcc = Join-Path $dir "riscv64-unknown-elf-gcc"
        if (Test-Path -LiteralPath $winGcc -PathType Leaf) {
            $fmt = Get-BinaryFormat $winGcc
            if ($fmt -eq "pe") {
                $toolchainBin = $dir
                $toolchainFormat = "pe"
                break
            }
        }
        if (Test-Path -LiteralPath $nixGcc -PathType Leaf) {
            $fmt = Get-BinaryFormat $nixGcc
            if (-not $toolchainBin) {
                $toolchainBin = $dir
                $toolchainFormat = $fmt
            }
        }
    }

    $usbipdExe = Find-FirstExistingFile @(
        (Find-CommandExe "usbipd"),
        (Join-Paths $env:ProgramFiles @("usbipd-win", "usbipd.exe"))
    )

    return [pscustomobject]@{
        SdkBase         = $SdkBase
        BLFlashCommand  = $blFlashExe
        PostProc        = $postProcExe
        Make            = $makeExe
        ToolchainBin    = $toolchainBin
        ToolchainFormat = $toolchainFormat
        Usbipd          = $usbipdExe
    }
}

function Get-WindowsDongleKernelDevices {
    param([psobject]$PythonInfo)
    if (-not $PythonInfo) {
        return $null
    }
    $toolsDir = Join-Path $Script:RepoRoot "tools"
    $pyCode = "import json, sys; sys.path.insert(0, sys.argv[1]); import usb_hid_check, usb_audio_check; ns = dict(u=usb_audio_check, audio=None, mic=None); exec('try: audio = u.find_windows_waveout_device()' + chr(10) + 'except Exception: pass' + chr(10) + 'try: mic = u.find_windows_wavein_device()' + chr(10) + 'except Exception: pass', ns); print(json.dumps(dict(hid=usb_hid_check.find_windows_hid_devices(), audio=ns['audio'], mic=ns['mic'])))"
    $res = Invoke-LoggedCommand -FilePath $PythonInfo.Executable -Arguments @("-c", $pyCode, $toolsDir)
    if ($res.ExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($res.Output)) {
        try {
            return ($res.Output | ConvertFrom-Json)
        } catch {
            return $null
        }
    }
    return $null
}

function Get-SerialPortsInfo {
    param([psobject]$PythonInfo)
    if ($PythonInfo -and $PythonInfo.PySerialAvailable) {
        if ($PythonInfo.PySerialSitePath) {
            $pyCode = "import json, sys; sys.path.insert(0, sys.argv[1]); import serial.tools.list_ports; print(json.dumps([dict(device=p.device, description=p.description, hwid=p.hwid) for p in serial.tools.list_ports.comports()]))"
            $res = Invoke-LoggedCommand -FilePath $PythonInfo.Executable -Arguments @("-c", $pyCode, $PythonInfo.PySerialSitePath)
        } else {
            $pyCode = "import json, serial.tools.list_ports; print(json.dumps([dict(device=p.device, description=p.description, hwid=p.hwid) for p in serial.tools.list_ports.comports()]))"
            $res = Invoke-LoggedCommand -FilePath $PythonInfo.Executable -Arguments @("-c", $pyCode)
        }
        if ($res.ExitCode -eq 0 -and -not [string]::IsNullOrWhiteSpace($res.Output)) {
            $parsed = $res.Output | ConvertFrom-Json
            return @($parsed)
        }
    }

    $portNames = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    return @($portNames | ForEach-Object {
        [pscustomobject]@{
            device      = $_
            description = "Serial Port"
            hwid        = ""
        }
    })
}

function Get-PythonScriptArguments {
    param(
        [Parameter(Mandatory = $true)][psobject]$PythonInfo,
        [Parameter(Mandatory = $true)][string]$ScriptPath,
        [string[]]$ScriptArgs = @()
    )
    if ($PythonInfo.PySerialSitePath) {
        $scriptDir = Split-Path -Parent $ScriptPath
        $shim = "import runpy, sys; sys.path.insert(0, sys.argv[1]); sys.path.insert(0, sys.argv[2]); sys.argv = sys.argv[3:]; runpy.run_path(sys.argv[0], run_name='__main__')"
        return @("-c", $shim, $PythonInfo.PySerialSitePath, $scriptDir, $ScriptPath) + $ScriptArgs
    }
    return @($ScriptPath) + $ScriptArgs
}

function Get-EnvironmentSnapshot {
    $vsRoot = Find-VisualStudioRoot
    $sdkBase = Find-BouffaloSdkBase
    $cmakeTools = Find-CMakeTools -VsRoot $vsRoot -SdkBase $sdkBase
    $compiler = Find-HostCompilerInfo -VsRoot $vsRoot
    $python = Find-PythonRuntime
    $node = Find-NodeRuntime
    $fwTools = Find-FirmwareTools -SdkBase $sdkBase

    return [pscustomobject]@{
        VsRoot        = $vsRoot
        CMakeTools    = $cmakeTools
        Compiler      = $compiler
        Python        = $python
        Node          = $node
        FirmwareTools = $fwTools
    }
}

function Invoke-Doctor {
    $envInfo = Get-EnvironmentSnapshot
    $ports = Get-SerialPortsInfo -PythonInfo $envInfo.Python
    $kernelDevs = Get-WindowsDongleKernelDevices -PythonInfo $envInfo.Python

    Write-Host "=== Windows Environment Diagnostics (uni-usb-dongle) ==="
    Write-Host "Repository Root : $Script:RepoRoot"
    Write-Host "PowerShell      : $($PSVersionTable.PSVersion) ($($PSVersionTable.PSEdition))"
    Write-Host ""

    Write-Host "[Host C Build Toolchain]"
    Write-Host "  Visual Studio : $(if ($envInfo.VsRoot) { $envInfo.VsRoot } else { 'NOT FOUND' })"
    Write-Host "  Compiler      : $(if ($envInfo.Compiler.Compiler) { "$($envInfo.Compiler.Kind) -> $($envInfo.Compiler.Compiler)" } else { 'MISSING' })"
    Write-Host "  CMake         : $(if ($envInfo.CMakeTools.CMake) { $envInfo.CMakeTools.CMake } else { 'MISSING' })"
    Write-Host "  CTest         : $(if ($envInfo.CMakeTools.CTest) { $envInfo.CMakeTools.CTest } else { 'MISSING' })"
    Write-Host "  Ninja         : $(if ($envInfo.CMakeTools.Ninja) { $envInfo.CMakeTools.Ninja } else { 'MISSING' })"
    Write-Host ""

    Write-Host "[Host Test Runtimes]"
    if ($envInfo.Python) {
        Write-Host "  Python        : $($envInfo.Python.Version) -> $($envInfo.Python.Executable)"
        if ($envInfo.Python.PySerialAvailable) {
            $srcNote = if ($envInfo.Python.PySerialSitePath) { " (via $($envInfo.Python.PySerialSitePath))" } else { "" }
            Write-Host "  pyserial      : $($envInfo.Python.PySerialVersion)$srcNote"
        } else {
            Write-Host "  pyserial      : MISSING (optional for HID-only audio firmware; required for COM port OTA/UART tools)"
        }
    } else {
        Write-Host "  Python        : MISSING (install Python 3.8+)"
    }
    if ($envInfo.Node) {
        $modeNote = if ($envInfo.Node.IsElectron) { " (Electron ELECTRON_RUN_AS_NODE=1)" } else { "" }
        Write-Host "  Node.js       : $($envInfo.Node.Version)$modeNote -> $($envInfo.Node.Executable)"
    } else {
        Write-Host "  Node.js       : MISSING (install Node.js 20+ for web unit tests)"
    }
    Write-Host ""

    Write-Host "[Firmware Build & Flash Tools (Native Windows)]"
    $fw = $envInfo.FirmwareTools
    Write-Host "  BL_SDK_BASE   : $(if ($fw.SdkBase) { $fw.SdkBase } else { 'NOT FOUND' })"
    Write-Host "  BLFlashCommand: $(if ($fw.BLFlashCommand) { $fw.BLFlashCommand } else { 'MISSING' })"
    Write-Host "  fw_post_proc  : $(if ($fw.PostProc) { $fw.PostProc } else { 'MISSING' })"
    Write-Host "  GNU Make      : $(if ($fw.Make) { $fw.Make } else { 'MISSING' })"
    if ($fw.ToolchainBin) {
        $tcDesc = if ($fw.ToolchainFormat -eq "pe") {
            "Windows native PE -> $($fw.ToolchainBin)"
        } else {
            "Unsupported format '$($fw.ToolchainFormat)' (requires Windows .exe PE toolchain) -> $($fw.ToolchainBin)"
        }
        Write-Host "  RISC-V GCC    : $tcDesc"
    } else {
        Write-Host "  RISC-V GCC    : MISSING"
    }
    Write-Host "  usbipd-win    : $(if ($fw.Usbipd) { $fw.Usbipd } else { 'Not found' })"
    Write-Host ""

    Write-Host "[Existing Firmware Artifacts]"
    $binPath = Join-Paths $Script:RepoRoot @("firmware", "build", "build_out", "uni_usb_dongle_bl616.bin")
    $otaPath = Join-Paths $Script:RepoRoot @("firmware", "build", "build_out", "uni_usb_dongle_bl616.xz.ota")
    foreach ($artPath in @($binPath, $otaPath)) {
        if (Test-Path -LiteralPath $artPath -PathType Leaf) {
            $item = Get-Item -LiteralPath $artPath
            $sha = Get-FileSha256 $artPath
            Write-Host "  $($item.Name) : $($item.Length) bytes, SHA-256 $sha"
        } else {
            Write-Host "  $(Split-Path -Leaf $artPath) : not present"
        }
    }
    Write-Host ""

    Write-Host "[Windows Kernel Dongle Interfaces (All Output Profiles)]"
    if ($kernelDevs -and ($kernelDevs.hid.Count -gt 0 -or $kernelDevs.audio -or $kernelDevs.mic)) {
        foreach ($h in $kernelDevs.hid) {
            $role = if ($h.usage_page -eq 0xFF00 -and $h.usage -eq 1) {
                "Management/OTA HID"
            } elseif ($h.usage_page -eq 1 -and $h.usage -eq 5) {
                "Game Controller HID"
            } else {
                "HID Collection"
            }
            Write-Host ("  {0,-18}: UsagePage=0x{1:X4} Usage=0x{2:X4} In={3} Feat={4} -> {5}" -f $role, $h.usage_page, $h.usage, $h.input_len, $h.feature_len, $h.path)
        }
        if ($kernelDevs.audio) {
            Write-Host "  USB Audio (waveOut): id=$($kernelDevs.audio[0]) '$($kernelDevs.audio[1])'"
        }
        if ($kernelDevs.mic) {
            Write-Host "  USB Mic (waveIn)   : id=$($kernelDevs.mic[0]) '$($kernelDevs.mic[1])'"
        }
    } else {
        Write-Host "  (not detected natively on Windows; connect dongle native USB J3 pins or detach from usbipd)"
    }
    Write-Host ""

    Write-Host "[Connected Serial Ports]"
    if ($ports.Count -eq 0) {
        Write-Host "  (none detected)"
    } else {
        foreach ($p in $ports) {
            $hwid = if ($p.hwid) { " [$($p.hwid)]" } else { "" }
            Write-Host "  $($p.device) : $($p.description)$hwid"
        }
    }

    $missingRequired = @()
    if (-not $envInfo.CMakeTools.CMake) { $missingRequired += "CMake" }
    if (-not $envInfo.CMakeTools.CTest) { $missingRequired += "CTest" }
    if (-not $envInfo.Compiler.Compiler) { $missingRequired += "C compiler (MSVC/GCC/Clang)" }
    if (-not $envInfo.Python) { $missingRequired += "Python 3.8+" }
    if (-not $envInfo.Node) { $missingRequired += "Node.js" }

    Write-Host ""
    if ($missingRequired.Count -gt 0) {
        Write-Host "Doctor status: MISSING REQUIRED HOST TOOLS -> $($missingRequired -join ', ')"
        return 1
    }
    Write-Host "Doctor status: READY for Windows-native host build, unit tests, kernel hardware tests, and OTA/UART flashing."
    if ($fw.ToolchainFormat -ne "pe") {
        Write-Host "Note: Native firmware cross-compilation requires a Windows-native Xuantie RISC-V toolchain (PE .exe)."
    }
    return 0
}

function Get-CachedCMakeBuildType {
    param([Parameter(Mandatory = $true)][string]$CachePath)
    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) {
        return $null
    }
    $line = Get-Content -LiteralPath $CachePath -ErrorAction SilentlyContinue |
        Where-Object { $_ -match "^CMAKE_BUILD_TYPE:STRING=" } | Select-Object -First 1
    if ($line) {
        return $line.Substring("CMAKE_BUILD_TYPE:STRING=".Length).Trim()
    }
    return $null
}

function Invoke-HostBuild {
    param(
        [Parameter(Mandatory = $true)][psobject]$EnvInfo,
        [Parameter(Mandatory = $true)][string]$ResolvedBuildDir,
        [Parameter(Mandatory = $true)][string]$BuildConfig
    )
    if (-not $EnvInfo.CMakeTools.CMake) {
        throw "CMake was not found. Install CMake 3.20+ or Visual Studio CMake tools."
    }
    if (-not $EnvInfo.Compiler.Compiler) {
        throw "No C compiler (MSVC cl.exe, gcc.exe, or clang.exe) was found."
    }

    Initialize-MsvcEnvironment -CompilerInfo $EnvInfo.Compiler

    $cmakeArgs = @("-S", $Script:RepoRoot, "-B", $ResolvedBuildDir)
    if ($EnvInfo.CMakeTools.Ninja) {
        $cmakeArgs += @("-G", "Ninja", "-DCMAKE_MAKE_PROGRAM=$($EnvInfo.CMakeTools.Ninja)")
    }
    $cmakeArgs += @(
        "-DCMAKE_BUILD_TYPE=$BuildConfig",
        "-DCMAKE_POLICY_DEFAULT_CMP0141=NEW",
        "-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded"
    )

    Write-Host "Configuring host CMake project ($BuildConfig) in $ResolvedBuildDir ..."
    $cfgRes = Invoke-LoggedCommand -FilePath $EnvInfo.CMakeTools.CMake -Arguments $cmakeArgs -StreamOutput
    if ($cfgRes.ExitCode -ne 0) {
        throw "CMake configure failed with exit code $($cfgRes.ExitCode)."
    }

    Write-Host "Building host C targets ($BuildConfig) ..."
    $bldRes = Invoke-LoggedCommand -FilePath $EnvInfo.CMakeTools.CMake -Arguments @(
        "--build", $ResolvedBuildDir, "--parallel"
    ) -StreamOutput
    if ($bldRes.ExitCode -ne 0) {
        throw "CMake build failed with exit code $($bldRes.ExitCode)."
    }

    Write-Host "Host build succeeded ($BuildConfig): $ResolvedBuildDir"
}

function Invoke-FirmwareBuild {
    param(
        [Parameter(Mandatory = $true)][psobject]$EnvInfo,
        [Parameter(Mandatory = $true)][string]$TargetBoard
    )
    $fw = $EnvInfo.FirmwareTools
    if (-not $fw.SdkBase) {
        throw "Bouffalo SDK (BL_SDK_BASE) was not found. Set `$env:BL_SDK_BASE or check out bouffalo_sdk adjacent to this repository."
    }
    if ($fw.ToolchainFormat -ne "pe") {
        $tcDetail = if ($fw.ToolchainBin) {
            "The discovered RISC-V toolchain at '$($fw.ToolchainBin)' is not a Windows PE (.exe) toolchain."
        } else {
            "No Windows RISC-V toolchain (riscv64-unknown-elf-gcc.exe) was found."
        }
        throw "$tcDetail Install a Windows-native Xuantie RISC-V toolchain (for example D:\Workspace\Xuantie-900-gcc-elf-newlib-mingw-V3.2.0) or set `$env:TOOLCHAIN_BIN."
    }
    if (-not $fw.Make) {
        throw "GNU Make (make.exe / mingw32-make.exe) was not found in PATH or BL_SDK_BASE\tools\make."
    }

    $fwBuildDir = Join-Paths $Script:RepoRoot @("firmware", "build")
    $fwCache = Join-Path $fwBuildDir "CMakeCache.txt"
    $fwMakefile = Join-Path $fwBuildDir "Makefile"
    if (Test-Path -LiteralPath $fwCache -PathType Leaf) {
        $cacheHead = Get-Content -LiteralPath $fwCache -TotalCount 30 -ErrorAction SilentlyContinue
        $cacheJoined = $cacheHead -join "`n"
        if ($cacheJoined -match "/mnt/|Unix Makefiles") {
            Write-Host "Cleaning stale non-Windows firmware build cache in $fwBuildDir ..."
            Remove-Item -LiteralPath $fwBuildDir -Recurse -Force
        }
    } elseif (Test-Path -LiteralPath $fwMakefile -PathType Leaf) {
        $mkHead = Get-Content -LiteralPath $fwMakefile -TotalCount 20 -ErrorAction SilentlyContinue
        if (($mkHead -join "`n") -match "/mnt/") {
            Write-Host "Cleaning stale non-Windows firmware Makefiles in $fwBuildDir ..."
            Remove-Item -LiteralPath $fwBuildDir -Recurse -Force
        }
    }

    $pathParts = New-Object System.Collections.Generic.List[string]
    $pathParts.Add($fw.ToolchainBin)
    $pathParts.Add((Split-Path -Parent $fw.Make))
    $sdkCmakeDir = Join-Paths $fw.SdkBase @("tools", "cmake", "bin")
    if (Test-Path -LiteralPath $sdkCmakeDir -PathType Container) { $pathParts.Add($sdkCmakeDir) }
    $sdkNinjaDir = Join-Paths $fw.SdkBase @("tools", "ninja")
    if (Test-Path -LiteralPath $sdkNinjaDir -PathType Container) { $pathParts.Add($sdkNinjaDir) }
    if ($EnvInfo.Python) {
        $pathParts.Add((Split-Path -Parent $EnvInfo.Python.Executable))
    }
    foreach ($entry in ($env:PATH -split ";")) {
        if ([string]::IsNullOrWhiteSpace($entry)) { continue }
        # CMake MinGW Makefiles refuses sh.exe in PATH.
        if (Test-Path -LiteralPath (Join-Path $entry "sh.exe") -PathType Leaf -ErrorAction SilentlyContinue) {
            continue
        }
        $pathParts.Add($entry)
    }

    $crossCompile = (Join-Path $fw.ToolchainBin "riscv64-unknown-elf-").Replace("\", "/")
    $envOverrides = @{
        BL_SDK_BASE = $fw.SdkBase.Replace("\", "/")
        PATH        = ($pathParts -join ";")
    }
    Write-Host "Building firmware natively on Windows for BOARD=$TargetBoard ..."
    $res = Invoke-LoggedCommand -FilePath $fw.Make -Arguments @(
        "-C", "firmware", "BOARD=$TargetBoard", "CROSS_COMPILE=$crossCompile"
    ) -EnvOverrides $envOverrides -StreamOutput
    if ($res.ExitCode -ne 0) {
        throw "Firmware build failed with exit code $($res.ExitCode)."
    }
}

function Invoke-BuildAction {
    $envInfo = Get-EnvironmentSnapshot
    $resolvedBuildDir = Resolve-RepoPath $BuildDir

    if ($Target -eq "Host" -or $Target -eq "All") {
        Invoke-HostBuild -EnvInfo $envInfo -ResolvedBuildDir $resolvedBuildDir -BuildConfig $Config
    }
    if ($Target -eq "Firmware" -or $Target -eq "All") {
        Invoke-FirmwareBuild -EnvInfo $envInfo -TargetBoard $Board
    }
    return 0
}

function ConvertFrom-JUnitXml {
    param(
        [Parameter(Mandatory = $true)][string]$XmlPath,
        [Parameter(Mandatory = $true)][string]$SuiteLabel
    )
    $results = New-Object System.Collections.Generic.List[object]
    if (-not (Test-Path -LiteralPath $XmlPath -PathType Leaf)) {
        return $results.ToArray()
    }
    [xml]$doc = Get-Content -LiteralPath $XmlPath -Raw
    $nodes = $doc.SelectNodes("//testcase")
    foreach ($node in $nodes) {
        $testName = [string]$node.GetAttribute("name")
        $className = [string]$node.GetAttribute("classname")
        $timeRaw = [string]$node.GetAttribute("time")
        $timeVal = 0.0
        [void][double]::TryParse($timeRaw, [System.Globalization.NumberStyles]::Float, [System.Globalization.CultureInfo]::InvariantCulture, [ref]$timeVal)

        $status = "passed"
        $message = ""
        $details = ""

        $failureNode = $node.SelectSingleNode("failure")
        $errorNode = $node.SelectSingleNode("error")
        $skippedNode = $node.SelectSingleNode("skipped")
        $sysOutNode = $node.SelectSingleNode("system-out")
        $sysErrNode = $node.SelectSingleNode("system-err")

        if ($failureNode) {
            $status = "failed"
            $message = [string]$failureNode.GetAttribute("message")
            $details = $failureNode.InnerText
        } elseif ($errorNode) {
            $status = "error"
            $message = [string]$errorNode.GetAttribute("message")
            $details = $errorNode.InnerText
        } elseif ($skippedNode) {
            $status = "skipped"
            $message = [string]$skippedNode.GetAttribute("message")
        }

        if (-not $details) {
            $parts = @()
            if ($sysOutNode -and -not [string]::IsNullOrWhiteSpace($sysOutNode.InnerText)) {
                $parts += $sysOutNode.InnerText.Trim()
            }
            if ($sysErrNode -and -not [string]::IsNullOrWhiteSpace($sysErrNode.InnerText)) {
                $parts += $sysErrNode.InnerText.Trim()
            }
            $details = ($parts -join [Environment]::NewLine)
        }

        $results.Add([pscustomobject]@{
            suite        = $SuiteLabel
            name         = $testName
            classname    = $className
            status       = $status
            duration_sec = [Math]::Round($timeVal, 6)
            message      = $message
            details      = $details
        })
    }
    return $results.ToArray()
}

function Write-CombinedJUnitXml {
    param(
        [Parameter(Mandatory = $true)][string]$OutputPath,
        [Parameter(Mandatory = $true)][object[]]$TestCases,
        [Parameter(Mandatory = $true)][double]$TotalDurationSec
    )
    $parent = Split-Path -Parent $OutputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.Encoding = [System.Text.UTF8Encoding]::new($false)

    $writer = [System.Xml.XmlWriter]::Create($OutputPath, $settings)
    try {
        $totalCount = $TestCases.Count
        $failCount = @($TestCases | Where-Object { $_.status -eq "failed" }).Count
        $errCount = @($TestCases | Where-Object { $_.status -eq "error" }).Count
        $skipCount = @($TestCases | Where-Object { $_.status -eq "skipped" }).Count

        $writer.WriteStartDocument()
        $writer.WriteStartElement("testsuites")
        $writer.WriteAttributeString("tests", [string]$totalCount)
        $writer.WriteAttributeString("failures", [string]$failCount)
        $writer.WriteAttributeString("errors", [string]$errCount)
        $writer.WriteAttributeString("skipped", [string]$skipCount)
        $writer.WriteAttributeString("time", $TotalDurationSec.ToString("0.0000", [System.Globalization.CultureInfo]::InvariantCulture))

        $groups = $TestCases | Group-Object -Property suite
        foreach ($group in $groups) {
            $cases = @($group.Group)
            $gFail = @($cases | Where-Object { $_.status -eq "failed" }).Count
            $gErr = @($cases | Where-Object { $_.status -eq "error" }).Count
            $gSkip = @($cases | Where-Object { $_.status -eq "skipped" }).Count
            $gTime = ($cases | Measure-Object -Property duration_sec -Sum).Sum
            if ($null -eq $gTime) { $gTime = 0.0 }

            $writer.WriteStartElement("testsuite")
            $writer.WriteAttributeString("name", [string]$group.Name)
            $writer.WriteAttributeString("tests", [string]$cases.Count)
            $writer.WriteAttributeString("failures", [string]$gFail)
            $writer.WriteAttributeString("errors", [string]$gErr)
            $writer.WriteAttributeString("skipped", [string]$gSkip)
            $writer.WriteAttributeString("time", ([double]$gTime).ToString("0.0000", [System.Globalization.CultureInfo]::InvariantCulture))

            foreach ($tc in $cases) {
                $writer.WriteStartElement("testcase")
                $writer.WriteAttributeString("classname", [string]$tc.classname)
                $writer.WriteAttributeString("name", [string]$tc.name)
                $writer.WriteAttributeString("time", ([double]$tc.duration_sec).ToString("0.000000", [System.Globalization.CultureInfo]::InvariantCulture))

                if ($tc.status -eq "failed") {
                    $writer.WriteStartElement("failure")
                    if ($tc.message) { $writer.WriteAttributeString("message", [string]$tc.message) }
                    if ($tc.details) { $writer.WriteString([string]$tc.details) }
                    $writer.WriteEndElement()
                } elseif ($tc.status -eq "error") {
                    $writer.WriteStartElement("error")
                    if ($tc.message) { $writer.WriteAttributeString("message", [string]$tc.message) }
                    if ($tc.details) { $writer.WriteString([string]$tc.details) }
                    $writer.WriteEndElement()
                } elseif ($tc.status -eq "skipped") {
                    $writer.WriteStartElement("skipped")
                    if ($tc.message) { $writer.WriteAttributeString("message", [string]$tc.message) }
                    $writer.WriteEndElement()
                }
                $writer.WriteEndElement()
            }
            $writer.WriteEndElement()
        }
        $writer.WriteEndElement()
        $writer.WriteEndDocument()
    } finally {
        $writer.Dispose()
    }
}

function Invoke-CTests {
    param(
        [Parameter(Mandatory = $true)][psobject]$EnvInfo,
        [Parameter(Mandatory = $true)][string]$ResolvedBuildDir,
        [Parameter(Mandatory = $true)][string]$ResolvedReportDir,
        [string]$FilterName
    )
    if (-not $EnvInfo.CMakeTools.CTest) {
        throw "CTest was not found."
    }

    $cachedConfig = Get-CachedCMakeBuildType (Join-Path $ResolvedBuildDir "CMakeCache.txt")
    $needsBuild = -not (Test-Path -LiteralPath (Join-Path $ResolvedBuildDir "CTestTestfile.cmake") -PathType Leaf)
    if (-not $needsBuild -and $Script:ConfigWasExplicit -and $cachedConfig -and ($cachedConfig -ne $Config)) {
        $needsBuild = $true
    }
    $effectiveConfig = if ($Script:ConfigWasExplicit -or -not $cachedConfig) { $Config } else { $cachedConfig }
    if ($needsBuild) {
        Invoke-HostBuild -EnvInfo $EnvInfo -ResolvedBuildDir $ResolvedBuildDir -BuildConfig $effectiveConfig
    }

    $junitPath = Join-Path $ResolvedReportDir "junit-ctest.xml"
    if (Test-Path -LiteralPath $junitPath) { Remove-Item -LiteralPath $junitPath -Force }

    $ctestArgs = @("--test-dir", $ResolvedBuildDir, "--output-on-failure", "--output-junit", $junitPath)
    if (-not [string]::IsNullOrWhiteSpace($FilterName)) {
        $ctestArgs += @("-R", [regex]::Escape($FilterName))
    }

    Write-Host "=== Running C host tests (CTest) ==="
    $res = Invoke-LoggedCommand -FilePath $EnvInfo.CMakeTools.CTest -Arguments $ctestArgs -StreamOutput
    $cases = @(ConvertFrom-JUnitXml -XmlPath $junitPath -SuiteLabel "C")
    if ($cases.Count -eq 0 -and $res.Output -match "No tests were found") {
        return [pscustomobject]@{
            ExitCode    = 0
            DurationSec = $res.DurationSec
            Output      = $res.Output
            Cases       = @()
        }
    }
    return [pscustomobject]@{
        ExitCode    = $res.ExitCode
        DurationSec = $res.DurationSec
        Output      = $res.Output
        Cases       = $cases
    }
}

function Invoke-PythonTests {
    param(
        [Parameter(Mandatory = $true)][psobject]$EnvInfo,
        [Parameter(Mandatory = $true)][string]$ResolvedReportDir,
        [string]$FilterName
    )
    if (-not $EnvInfo.Python) {
        throw "Python 3.8+ was not found. Install Python 3.8+ or set `$env:PYTHON_EXE."
    }

    $pyJsonPath = Join-Path $ResolvedReportDir "python-results.json"
    $pyJunitPath = Join-Path $ResolvedReportDir "junit-python.xml"
    $pyRunnerPath = Join-Path $ResolvedReportDir "run_python_tests.py"

    $runnerCode = @'
import argparse
import json
import sys
import time
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--repo", required=True)
parser.add_argument("--json-out", required=True)
parser.add_argument("--junit-out", required=True)
parser.add_argument("--filter", default="")
parser.add_argument("--extra-site", default="")
args = parser.parse_args()

repo_root = args.repo
filter_name = args.filter
json_out = Path(args.json_out)
junit_out = Path(args.junit_out)
extra_site = args.extra_site

sys.path.insert(0, repo_root)
if extra_site:
    sys.path.insert(1, extra_site)

def iter_cases(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            for sub in iter_cases(item):
                yield sub
        else:
            yield item

loader = unittest.TestLoader()
discovered = loader.discover(str(Path(repo_root) / "tests"), pattern="test_*.py")
all_cases = list(iter_cases(discovered))
if filter_name:
    needle = filter_name.lower()
    selected = [case for case in all_cases if needle in case.id().lower()]
else:
    selected = all_cases

class RecordingResult(unittest.TestResult):
    def __init__(self):
        super().__init__()
        self.records = []
        self._started = {}

    def startTest(self, test):
        super().startTest(test)
        self._started[test.id()] = time.perf_counter()

    def addSuccess(self, test):
        super().addSuccess(test)
        dur = time.perf_counter() - self._started.get(test.id(), time.perf_counter())
        self.records.append({"suite": "Python", "name": test.id(), "classname": test.__class__.__module__ + "." + test.__class__.__name__, "status": "passed", "duration_sec": round(dur, 6), "message": "", "details": ""})
        print(f"{test.id()} ... ok ({dur:.4f}s)")

    def addFailure(self, test, err):
        super().addFailure(test, err)
        dur = time.perf_counter() - self._started.get(test.id(), time.perf_counter())
        tb = self._exc_info_to_string(err, test)
        self.records.append({"suite": "Python", "name": test.id(), "classname": test.__class__.__module__ + "." + test.__class__.__name__, "status": "failed", "duration_sec": round(dur, 6), "message": str(err[1]), "details": tb})
        print(f"{test.id()} ... FAIL ({dur:.4f}s)")

    def addError(self, test, err):
        super().addError(test, err)
        dur = time.perf_counter() - self._started.get(test.id(), time.perf_counter())
        tb = self._exc_info_to_string(err, test)
        self.records.append({"suite": "Python", "name": test.id(), "classname": test.__class__.__module__ + "." + test.__class__.__name__, "status": "error", "duration_sec": round(dur, 6), "message": str(err[1]), "details": tb})
        print(f"{test.id()} ... ERROR ({dur:.4f}s)")

    def addSkip(self, test, reason):
        super().addSkip(test, reason)
        dur = time.perf_counter() - self._started.get(test.id(), time.perf_counter())
        self.records.append({"suite": "Python", "name": test.id(), "classname": test.__class__.__module__ + "." + test.__class__.__name__, "status": "skipped", "duration_sec": round(dur, 6), "message": reason, "details": ""})
        print(f"{test.id()} ... skipped ({reason})")

suite = unittest.TestSuite(selected)
result = RecordingResult()
t0 = time.perf_counter()
suite.run(result)
total_dur = time.perf_counter() - t0

for test, tb in result.failures + result.errors:
    print(f"=== {test.id()} ===", file=sys.stderr)
    print(tb, file=sys.stderr)

json_out.write_text(json.dumps({"tests": result.records, "duration_sec": round(total_dur, 6)}, indent=2), encoding="utf-8")

ts = ET.Element("testsuite", {
    "name": "Python",
    "tests": str(len(result.records)),
    "failures": str(len(result.failures)),
    "errors": str(len(result.errors)),
    "skipped": str(len(result.skipped)),
    "time": f"{total_dur:.6f}",
})
for rec in result.records:
    tc = ET.SubElement(ts, "testcase", {
        "classname": rec["classname"],
        "name": rec["name"],
        "time": f"{rec['duration_sec']:.6f}",
    })
    if rec["status"] == "failed":
        node = ET.SubElement(tc, "failure", {"message": rec["message"]})
        node.text = rec["details"]
    elif rec["status"] == "error":
        node = ET.SubElement(tc, "error", {"message": rec["message"]})
        node.text = rec["details"]
    elif rec["status"] == "skipped":
        ET.SubElement(tc, "skipped", {"message": rec["message"]})

ET.ElementTree(ts).write(junit_out, encoding="utf-8", xml_declaration=True)
print(f"Ran {len(result.records)} Python tests in {total_dur:.3f}s")
sys.exit(0 if result.wasSuccessful() else 1)
'@

    Write-Utf8NoBom -Path $pyRunnerPath -Content $runnerCode
    $pyArgs = New-Object System.Collections.Generic.List[string]
    $pyArgs.Add($pyRunnerPath)
    $pyArgs.Add("--repo"); $pyArgs.Add($Script:RepoRoot)
    $pyArgs.Add("--json-out"); $pyArgs.Add($pyJsonPath)
    $pyArgs.Add("--junit-out"); $pyArgs.Add($pyJunitPath)
    if (-not [string]::IsNullOrWhiteSpace($FilterName)) {
        $pyArgs.Add("--filter"); $pyArgs.Add($FilterName)
    }
    if (-not [string]::IsNullOrWhiteSpace($EnvInfo.Python.PySerialSitePath)) {
        $pyArgs.Add("--extra-site"); $pyArgs.Add($EnvInfo.Python.PySerialSitePath)
    }

    Write-Host "=== Running Python host tests ==="
    $res = Invoke-LoggedCommand -FilePath $EnvInfo.Python.Executable -Arguments $pyArgs.ToArray() -StreamOutput

    $parsed = Get-Content -LiteralPath $pyJsonPath -Raw | ConvertFrom-Json
    return [pscustomobject]@{
        ExitCode    = $res.ExitCode
        DurationSec = $res.DurationSec
        Output      = $res.Output
        Cases       = @($parsed.tests)
    }
}

function Invoke-WebTests {
    param(
        [Parameter(Mandatory = $true)][psobject]$EnvInfo,
        [Parameter(Mandatory = $true)][string]$ResolvedReportDir,
        [string]$FilterName
    )
    if (-not $EnvInfo.Node) {
        throw "Node.js (or VS Code / Electron Node mode) was not found."
    }

    $allFiles = @(
        (Join-Paths $Script:RepoRoot @("tests", "test_web_protocol.mjs")),
        (Join-Paths $Script:RepoRoot @("tests", "test_web_hid.mjs")),
        (Join-Paths $Script:RepoRoot @("tests", "test_web_radio.mjs"))
    )

    $selectedFiles = $allFiles
    $useNamePattern = $false
    if (-not [string]::IsNullOrWhiteSpace($FilterName)) {
        $fileMatches = @($allFiles | Where-Object { (Split-Path -Leaf $_).ToLowerInvariant().Contains($FilterName.ToLowerInvariant()) })
        if ($fileMatches.Count -gt 0) {
            $selectedFiles = $fileMatches
        } else {
            $useNamePattern = $true
        }
    }

    $junitPath = Join-Path $ResolvedReportDir "junit-web.xml"
    if (Test-Path -LiteralPath $junitPath) { Remove-Item -LiteralPath $junitPath -Force }

    $nodeArgs = New-Object System.Collections.Generic.List[string]
    $nodeArgs.Add("--test")
    if ($useNamePattern) {
        $nodeArgs.Add("--test-name-pattern=$([regex]::Escape($FilterName))")
    }
    if ($EnvInfo.Node.Major -ge 20) {
        $nodeArgs.Add("--test-reporter=spec")
        $nodeArgs.Add("--test-reporter-destination=stdout")
        $nodeArgs.Add("--test-reporter=junit")
        $nodeArgs.Add("--test-reporter-destination=$junitPath")
    }
    foreach ($f in $selectedFiles) {
        $nodeArgs.Add($f)
    }

    Write-Host "=== Running Web (Node.js) host tests ==="
    $res = Invoke-LoggedCommand -FilePath $EnvInfo.Node.Executable -Arguments $nodeArgs.ToArray() `
        -EnvOverrides $EnvInfo.Node.Env -StreamOutput

    $cases = @(ConvertFrom-JUnitXml -XmlPath $junitPath -SuiteLabel "Web")
    if ($useNamePattern) {
        $cases = @($cases | Where-Object { $_.name -notmatch "\.mjs$" })
    }
    return [pscustomobject]@{
        ExitCode    = $res.ExitCode
        DurationSec = $res.DurationSec
        Output      = $res.Output
        Cases       = $cases
    }
}

function Invoke-HardwareTests {
    param(
        [Parameter(Mandatory = $true)][psobject]$EnvInfo,
        [string]$PortName,
        [string]$FilterName
    )
    if (-not $EnvInfo.Python) {
        throw "Python 3.8+ is required for Windows kernel hardware checks."
    }

    $cases = New-Object System.Collections.Generic.List[object]
    $overallExit = 0
    $outputs = New-Object System.Collections.Generic.List[string]
    $totalDur = 0.0

    $checks = New-Object System.Collections.Generic.List[object]
    $checks.Add([pscustomobject]@{
        Name   = "hardware.usb_hid_check"
        Script = (Join-Paths $Script:RepoRoot @("tools", "usb_hid_check.py"))
        Args   = @("--device", "auto")
    })
    $checks.Add([pscustomobject]@{
        Name   = "hardware.usb_gamepad_check"
        Script = (Join-Paths $Script:RepoRoot @("tools", "usb_gamepad_check.py"))
        Args   = @("--hid", "auto", "--cycles", "2")
    })
    $checks.Add([pscustomobject]@{
        Name   = "hardware.usb_audio_check"
        Script = (Join-Paths $Script:RepoRoot @("tools", "usb_audio_check.py"))
        Args   = @("--device", "auto", "--seconds", "3")
    })
    $checks.Add([pscustomobject]@{
        Name   = "hardware.bluetooth_check"
        Script = (Join-Paths $Script:RepoRoot @("tools", "bluetooth_check.py"))
        Args   = @("--device", "auto")
    })

    if (-not [string]::IsNullOrWhiteSpace($PortName)) {
        if (-not $EnvInfo.Python.PySerialAvailable) {
            throw "Python 3.8+ with pyserial is required for COM port checks ($PortName)."
        }
        $checks.Add([pscustomobject]@{
            Name   = "hardware.usb_ota_probe"
            Script = (Join-Paths $Script:RepoRoot @("tools", "usb_ota.py"))
            Args   = @("--port", $PortName, "--probe")
        })
        $checks.Add([pscustomobject]@{
            Name   = "hardware.usb_device_check"
            Script = (Join-Paths $Script:RepoRoot @("tools", "usb_device_check.py"))
            Args   = @("--port", $PortName)
        })
    }

    foreach ($chk in $checks) {
        if (-not [string]::IsNullOrWhiteSpace($FilterName) -and -not $chk.Name.ToLowerInvariant().Contains($FilterName.ToLowerInvariant())) {
            continue
        }
        Write-Host "=== Running Windows Kernel Hardware Check: $($chk.Name) ==="
        $hwArgs = Get-PythonScriptArguments -PythonInfo $EnvInfo.Python -ScriptPath $chk.Script -ScriptArgs $chk.Args
        $res = Invoke-LoggedCommand -FilePath $EnvInfo.Python.Executable `
            -Arguments $hwArgs -StreamOutput
        $outputs.Add($res.Output)
        $totalDur += $res.DurationSec
        $status = if ($res.ExitCode -eq 0) { "passed" } else { $overallExit = 1; "failed" }
        $cases.Add([pscustomobject]@{
            suite        = "Hardware"
            name         = $chk.Name
            classname    = "hardware.windows"
            status       = $status
            duration_sec = $res.DurationSec
            message      = if ($status -eq "passed") { "" } else { "Exited with code $($res.ExitCode)" }
            details      = $res.Output
        })
    }

    return [pscustomobject]@{
        ExitCode    = $overallExit
        DurationSec = [Math]::Round($totalDur, 4)
        Output      = ($outputs -join [Environment]::NewLine)
        Cases       = $cases.ToArray()
    }
}

function Invoke-TestAction {
    $envInfo = Get-EnvironmentSnapshot
    $resolvedBuildDir = Resolve-RepoPath $BuildDir
    $resolvedReportDir = if ([string]::IsNullOrWhiteSpace($ReportDir)) {
        Join-Path $resolvedBuildDir "test-results"
    } else {
        Resolve-RepoPath $ReportDir
    }
    if (-not (Test-Path -LiteralPath $resolvedReportDir -PathType Container)) {
        New-Item -ItemType Directory -Path $resolvedReportDir -Force | Out-Null
    }

    $runC = ($Suite -eq "All" -or $Suite -eq "C")
    $runPy = ($Suite -eq "All" -or $Suite -eq "Python")
    $runWeb = ($Suite -eq "All" -or $Suite -eq "Web")
    $runHw = ($Suite -eq "Hardware" -or $IncludeHardware)

    $allCases = New-Object System.Collections.Generic.List[object]
    $logSections = New-Object System.Collections.Generic.List[string]
    $overallSw = [System.Diagnostics.Stopwatch]::StartNew()
    $overallExit = 0

    if ($runC) {
        $cRes = Invoke-CTests -EnvInfo $envInfo -ResolvedBuildDir $resolvedBuildDir -ResolvedReportDir $resolvedReportDir -FilterName $Name
        if ($cRes.ExitCode -ne 0) { $overallExit = 1 }
        foreach ($c in $cRes.Cases) { $allCases.Add($c) }
        $logSections.Add("=== C SUITE ===`r`n$($cRes.Output)")
    }

    if ($runPy) {
        $pyRes = Invoke-PythonTests -EnvInfo $envInfo -ResolvedReportDir $resolvedReportDir -FilterName $Name
        if ($pyRes.ExitCode -ne 0) { $overallExit = 1 }
        foreach ($c in $pyRes.Cases) { $allCases.Add($c) }
        $logSections.Add("=== PYTHON SUITE ===`r`n$($pyRes.Output)")
    }

    if ($runWeb) {
        $webRes = Invoke-WebTests -EnvInfo $envInfo -ResolvedReportDir $resolvedReportDir -FilterName $Name
        if ($webRes.ExitCode -ne 0) { $overallExit = 1 }
        foreach ($c in $webRes.Cases) { $allCases.Add($c) }
        $logSections.Add("=== WEB SUITE ===`r`n$($webRes.Output)")
    }

    if ($runHw) {
        $hwRes = Invoke-HardwareTests -EnvInfo $envInfo -PortName $Device -FilterName $Name
        if ($hwRes.ExitCode -ne 0) { $overallExit = 1 }
        foreach ($c in $hwRes.Cases) { $allCases.Add($c) }
        $logSections.Add("=== HARDWARE SUITE ===`r`n$($hwRes.Output)")
    } else {
        $hwNotice = "Physical hardware checks (Windows kernel HID, gamepad, USB audio, and Bluetooth checks) were not executed. Pass -Suite Hardware to run them."
        $allCases.Add([pscustomobject]@{
            suite        = "Hardware"
            name         = "hardware.physical_device_checks"
            classname    = "hardware.windows"
            status       = "skipped"
            duration_sec = 0.0
            message      = $hwNotice
            details      = ""
        })
    }

    $overallSw.Stop()
    $totalDuration = [Math]::Round($overallSw.Elapsed.TotalSeconds, 4)

    $executedCases = @($allCases | Where-Object { $_.status -ne "skipped" -or $_.suite -ne "Hardware" })
    if (-not [string]::IsNullOrWhiteSpace($Name) -and $executedCases.Count -eq 0) {
        Write-Host "No tests matched filter '$Name' across selected suite(s) ($Suite)."
        $overallExit = 1
    }

    $passedCount = @($allCases | Where-Object { $_.status -eq "passed" }).Count
    $failedCount = @($allCases | Where-Object { $_.status -eq "failed" }).Count
    $errorCount = @($allCases | Where-Object { $_.status -eq "error" }).Count
    $skippedCount = @($allCases | Where-Object { $_.status -eq "skipped" }).Count

    $jsonReportPath = Join-Path $resolvedReportDir "test-report.json"
    $xmlReportPath = Join-Path $resolvedReportDir "test-report.xml"
    $logReportPath = Join-Path $resolvedReportDir "test-output.log"

    $reportObj = [ordered]@{
        generated_at_utc = [DateTime]::UtcNow.ToString("o")
        platform         = "windows"
        suite            = $Suite
        filter           = $Name
        duration_sec     = $totalDuration
        summary          = [ordered]@{
            total   = $allCases.Count
            passed  = $passedCount
            failed  = $failedCount
            errors  = $errorCount
            skipped = $skippedCount
        }
        tests            = $allCases.ToArray()
    }
    Write-Utf8NoBom -Path $jsonReportPath -Content ($reportObj | ConvertTo-Json -Depth 10)
    Write-CombinedJUnitXml -OutputPath $xmlReportPath -TestCases $allCases.ToArray() -TotalDurationSec $totalDuration
    Write-Utf8NoBom -Path $logReportPath -Content ($logSections -join "`r`n`r`n")

    Write-Host ""
    Write-Host "=== Test Summary ($totalDuration s) ==="
    Write-Host "  Passed : $passedCount | Failed : $failedCount | Errors : $errorCount | Skipped : $skippedCount"
    Write-Host "  JSON   : $jsonReportPath"
    Write-Host "  JUnit  : $xmlReportPath"
    Write-Host "  Log    : $logReportPath"

    return $overallExit
}

function Test-OtaFirmwareImage {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Firmware OTA image does not exist: $Path"
    }
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 512) {
        throw "OTA image is shorter than its 512-byte header ($($bytes.Length) bytes): $Path"
    }
    $magic = [System.Text.Encoding]::ASCII.GetString($bytes, 0, 9)
    if ($magic -ne "BL60X_OTA") {
        throw "Unrecognized Bouffalo OTA image header (expected 'BL60X_OTA', got '$magic'): $Path"
    }
    $fmt = [System.Text.Encoding]::ASCII.GetString($bytes, 16, 2)
    if ($fmt -ne "XZ") {
        throw "Only SDK XZ OTA packages are supported (found '$fmt'): $Path"
    }
    $bodySize = [System.BitConverter]::ToUInt32($bytes, 20)
    if ($bodySize -eq 0 -or $bytes.Length -ne (512 + $bodySize)) {
        throw "OTA package size ($($bytes.Length)) does not match header body size ($bodySize + 512): $Path"
    }
    return [pscustomobject]@{
        Path      = $Path
        SizeBytes = $bytes.Length
        BodyBytes = $bodySize
        Sha256    = Get-FileSha256 $Path
    }
}

function Test-UartSlot0FirmwareImage {
    param([Parameter(Mandatory = $true)][string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Firmware binary image does not exist: $Path"
    }
    $item = Get-Item -LiteralPath $Path
    if ($item.Length -le 0) {
        throw "Firmware binary image is empty: $Path"
    }
    if ($item.Length -gt $Script:M62Slot0LimitBytes) {
        throw "M62 app image is $($item.Length) bytes; slot 0 limit is $($Script:M62Slot0LimitBytes) bytes (0x200000)."
    }

    $iniPath = Join-Paths $Script:RepoRoot @("firmware", "flash_prog_cfg_m62_app.ini")
    if (-not (Test-Path -LiteralPath $iniPath -PathType Leaf)) {
        throw "Missing M62 flash config: $iniPath"
    }
    $iniText = Get-Content -LiteralPath $iniPath -Raw
    if ($iniText -notmatch "(?m)^erase\s*=\s*1\b" -or
        $iniText -notmatch "(?m)^boot2_isp_mode\s*=\s*0\b" -or
        $iniText -notmatch "(?m)^address\s*=\s*0x00010000\b") {
        throw "M62 flash config ($iniPath) failed safety validation (must use erase=1, boot2_isp_mode=0, address=0x00010000)."
    }

    return [pscustomobject]@{
        Path       = $Path
        SizeBytes  = $item.Length
        Sha256     = Get-FileSha256 $Path
        ConfigPath = $iniPath
    }
}

function Resolve-WindowsComPort {
    param(
        [string]$PortValue,
        [Parameter(Mandatory = $true)][string]$ContextLabel
    )
    if ([string]::IsNullOrWhiteSpace($PortValue)) {
        throw "Target serial port is required for $ContextLabel. Specify -Device COMx (e.g. -Device COM5 for CDC OTA or -Device COM7 for CH340 UART)."
    }
    if ($PortValue -notmatch "^(?:\\\\\.\\)?COM[1-9]\d*$") {
        throw "Invalid Windows serial port '$PortValue'. Expected a Windows COM port such as COM5 or COM7."
    }
    return $PortValue.ToUpperInvariant()
}

function Invoke-FlashAction {
    if ($Board -ne "m62_12f") {
        throw "BOARD=$Board is a reference-board smoke configuration and must not be flashed to the M62-12F-Kit."
    }

    $resolvedMethod = $Method
    if ($resolvedMethod -eq "auto") {
        if ($Probe) {
            if (-not [string]::IsNullOrWhiteSpace($Device) -and $Device -ne "auto") {
                $resolvedMethod = "ota"
            } else {
                $resolvedMethod = "hid"
            }
        } elseif (-not [string]::IsNullOrWhiteSpace($Firmware) -and $Firmware.ToLowerInvariant().EndsWith(".bin")) {
            $resolvedMethod = "uart"
        } elseif ([string]::IsNullOrWhiteSpace($Firmware) -or $Firmware.ToLowerInvariant().EndsWith(".ota")) {
            if (-not [string]::IsNullOrWhiteSpace($Device) -and $Device -ne "auto" -and $Device -notmatch "^\\\\\?\\hid") {
                $resolvedMethod = "ota"
            } else {
                $resolvedMethod = "hid"
            }
        } else {
            throw "Cannot infer flash method from '$Firmware'. Specify -Method hid, ota, or uart."
        }
    }

    $normalizedPort = ""
    if ($resolvedMethod -eq "hid") {
        $normalizedPort = if ([string]::IsNullOrWhiteSpace($Device)) { "auto" } else { $Device }
    } else {
        $normalizedPort = Resolve-WindowsComPort -PortValue $Device -ContextLabel "method '$resolvedMethod'"
    }

    $envInfo = Get-EnvironmentSnapshot
    $resolvedFirmware = $null
    if (-not $Probe) {
        if ([string]::IsNullOrWhiteSpace($Firmware)) {
            $defaultRel = if ($resolvedMethod -eq "uart") {
                Join-Paths "firmware" @("build", "build_out", "uni_usb_dongle_bl616.bin")
            } else {
                Join-Paths "firmware" @("build", "build_out", "uni_usb_dongle_bl616.xz.ota")
            }
            $resolvedFirmware = Resolve-RepoPath $defaultRel
        } else {
            $resolvedFirmware = Resolve-InputPath $Firmware
        }
    }

    $validation = $null
    $commandDisplay = ""
    $safetyNote = ""

    if ($resolvedMethod -eq "hid") {
        $pyExe = if ($envInfo.Python) { $envInfo.Python.Executable } else { "python" }
        $hidScript = Join-Paths $Script:RepoRoot @("tools", "usb_hid_check.py")
        if (-not $Probe) {
            $validation = Test-OtaFirmwareImage $resolvedFirmware
            $safetyNote = "Windows HID feature-report A/B OTA: preserves Boot2, but inactive FW slot 1 aliases 'mfg' (0x210000)."
            $commandDisplay = "`"$pyExe`" `"$hidScript`" --device $normalizedPort --image `"$resolvedFirmware`""
        } else {
            $safetyNote = "Non-writing Windows HID probe: verifies management feature reports without writing flash."
            $commandDisplay = "`"$pyExe`" `"$hidScript`" --device $normalizedPort"
        }
    } elseif ($resolvedMethod -eq "ota") {
        if (-not $Probe) {
            $validation = Test-OtaFirmwareImage $resolvedFirmware
            $safetyNote = "Native USB CDC-ACM A/B OTA: preserves Boot2, but inactive FW slot 1 aliases 'mfg' (0x210000)."
        } else {
            $safetyNote = "Non-writing UOTA probe: verifies USB CDC-ACM framing without writing flash."
        }
        $pyExe = if ($envInfo.Python) { $envInfo.Python.Executable } else { "python" }
        $otaScript = Join-Paths $Script:RepoRoot @("tools", "usb_ota.py")
        if ($Probe) {
            $commandDisplay = "`"$pyExe`" `"$otaScript`" --port $normalizedPort --probe"
        } else {
            $commandDisplay = "`"$pyExe`" `"$otaScript`" --port $normalizedPort --image `"$resolvedFirmware`""
        }
    } else {
        if ($Probe) {
            throw "-Probe is only supported with -Method hid or -Method ota."
        }
        $validation = Test-UartSlot0FirmwareImage $resolvedFirmware
        $blFlash = $envInfo.FirmwareTools.BLFlashCommand
        if (-not $blFlash) {
            throw "BLFlashCommand.exe was not found under BL_SDK_BASE\tools\bflb_tools\bouffalo_flash_cube."
        }
        $safetyNote = "App-only UART slot-0 programming at 0x10000 (erase=1, boot2_isp_mode=0, max 0x200000 bytes). Requires ROM download mode."
        $defaultBin = Resolve-RepoPath (Join-Paths "firmware" @("build", "build_out", "uni_usb_dongle_bl616.bin"))
        $cfgToUse = if ([string]::Equals($resolvedFirmware, $defaultBin, [System.StringComparison]::OrdinalIgnoreCase)) {
            $validation.ConfigPath
        } else {
            $resolvedBuildDir = Resolve-RepoPath $BuildDir
            Join-Path $resolvedBuildDir "flash_prog_cfg_m62_app.ini"
        }
        $commandDisplay = "`"$blFlash`" --interface=uart --baudrate=$BaudRate --port=$normalizedPort --chipname=bl616 --config=`"$cfgToUse`" --reset"
    }

    Write-Host "=== Flash Pre-Flight Validation ==="
    Write-Host "  Target Board : $Board (chip=bl616)"
    Write-Host "  Method       : $resolvedMethod"
    Write-Host "  Device       : $normalizedPort"
    if ($validation) {
        Write-Host "  Firmware     : $($validation.Path)"
        Write-Host "  Size         : $($validation.SizeBytes) bytes"
        Write-Host "  SHA-256      : $($validation.Sha256)"
    } else {
        Write-Host "  Mode         : Protocol Probe (no image write)"
    }
    Write-Host "  Boundary     : $safetyNote"
    Write-Host "  Command      : $commandDisplay"

    if ($DryRun) {
        Write-Host "Dry-run validation passed. No device was opened and no flash was modified."
        return 0
    }

    if ($Probe) {
        if (-not $envInfo.Python) {
            throw "Python 3.8+ is required to run protocol probe."
        }
        if ($resolvedMethod -eq "hid") {
            $hidScript = Join-Paths $Script:RepoRoot @("tools", "usb_hid_check.py")
            $res = Invoke-LoggedCommand -FilePath $envInfo.Python.Executable `
                -Arguments @($hidScript, "--device", $normalizedPort) -StreamOutput
            if ($res.ExitCode -ne 0) {
                throw "HID protocol probe failed with exit code $($res.ExitCode)."
            }
            return 0
        }
        if (-not $envInfo.Python.PySerialAvailable) {
            throw "Python 3.8+ with pyserial is required to run usb_ota.py --probe."
        }
        $otaScript = Join-Paths $Script:RepoRoot @("tools", "usb_ota.py")
        $probeArgs = Get-PythonScriptArguments -PythonInfo $envInfo.Python -ScriptPath $otaScript -ScriptArgs @("--port", $normalizedPort, "--probe")
        $res = Invoke-LoggedCommand -FilePath $envInfo.Python.Executable `
            -Arguments $probeArgs -StreamOutput
        if ($res.ExitCode -ne 0) {
            throw "OTA protocol probe failed with exit code $($res.ExitCode)."
        }
        return 0
    }

    if (-not $ConfirmFlash) {
        throw "Refusing to write flash on physical device '$normalizedPort' without explicit authorization. Re-run with -ConfirmFlash to flash, or pass -DryRun / -Probe."
    }

    if ($resolvedMethod -eq "hid") {
        if (-not $envInfo.Python) {
            throw "Python 3.8+ is required for Windows HID OTA."
        }
        $hidScript = Join-Paths $Script:RepoRoot @("tools", "usb_hid_check.py")
        $res = Invoke-LoggedCommand -FilePath $envInfo.Python.Executable `
            -Arguments @($hidScript, "--device", $normalizedPort, "--image", $resolvedFirmware) -StreamOutput
        if ($res.ExitCode -ne 0) {
            throw "HID OTA flash failed with exit code $($res.ExitCode)."
        }
        Write-Host "HID OTA flash completed. Re-run '.\tools\windows.ps1 test -Suite Hardware' after reboot to verify device state."
        return 0
    }

    if ($resolvedMethod -eq "ota") {
        if (-not $envInfo.Python -or -not $envInfo.Python.PySerialAvailable) {
            throw "Python 3.8+ with pyserial is required for native USB OTA."
        }
        $otaScript = Join-Paths $Script:RepoRoot @("tools", "usb_ota.py")
        $otaArgs = Get-PythonScriptArguments -PythonInfo $envInfo.Python -ScriptPath $otaScript -ScriptArgs @("--port", $normalizedPort, "--image", $resolvedFirmware)
        $res = Invoke-LoggedCommand -FilePath $envInfo.Python.Executable `
            -Arguments $otaArgs -StreamOutput
        if ($res.ExitCode -ne 0) {
            throw "OTA flash failed with exit code $($res.ExitCode)."
        }
        Write-Host "OTA flash completed. Re-run '.\tools\windows.ps1 test -Suite Hardware -Device $normalizedPort' after reboot to verify device state."
        return 0
    }

    $blFlashExe = $envInfo.FirmwareTools.BLFlashCommand
    $defaultBinPath = Resolve-RepoPath (Join-Paths "firmware" @("build", "build_out", "uni_usb_dongle_bl616.bin"))
    $workDir = Join-Path $Script:RepoRoot "firmware"
    $cfgArg = "flash_prog_cfg_m62_app.ini"

    if (-not [string]::Equals($resolvedFirmware, $defaultBinPath, [System.StringComparison]::OrdinalIgnoreCase)) {
        $resolvedBuildDir = Resolve-RepoPath $BuildDir
        $tempCfg = Join-Path $resolvedBuildDir "flash_prog_cfg_m62_app.ini"
        $customIni = @"
[cfg]
# 0: no erase, 1: erase only the programmed section, 2: chip erase
erase = 1
skip_mode = 0x0, 0x0
boot2_isp_mode = 0

[FW]
filedir = $resolvedFirmware
address = 0x00010000
"@
        Write-Utf8NoBom -Path $tempCfg -Content $customIni
        $cfgArg = $tempCfg
    }

    $flashRes = Invoke-LoggedCommand -FilePath $blFlashExe -WorkingDirectory $workDir -Arguments @(
        "--interface=uart",
        "--baudrate=$BaudRate",
        "--port=$normalizedPort",
        "--chipname=bl616",
        "--config=$cfgArg",
        "--reset"
    ) -StreamOutput
    if ($flashRes.ExitCode -ne 0) {
        throw "BLFlashCommand.exe failed with exit code $($flashRes.ExitCode)."
    }
    Write-Host "UART slot-0 programming completed and reset requested."
    return 0
}

function Invoke-BackupAction {
    if ($Board -ne "m62_12f") {
        throw "BOARD=$Board is not supported for M62 UART flash backup; use -Board m62_12f."
    }

    $normalizedPort = Resolve-WindowsComPort -PortValue $Device -ContextLabel "UART flash backup"
    $envInfo = Get-EnvironmentSnapshot
    $blFlashExe = $envInfo.FirmwareTools.BLFlashCommand
    if (-not $blFlashExe) {
        throw "BLFlashCommand.exe was not found under BL_SDK_BASE\tools\bflb_tools\bouffalo_flash_cube."
    }

    $regionSpec = switch ($Region) {
        "table" { [pscustomobject]@{ Start = "0x0000E000"; Length = "0x00002000"; ExpectedBytes = 8192 } }
        "boot2" { [pscustomobject]@{ Start = "0x00000000"; Length = "0x0000E000"; ExpectedBytes = 57344 } }
        "full"  { [pscustomobject]@{ Start = "0x00000000"; Length = "0x00400000"; ExpectedBytes = 4194304 } }
    }

    $resolvedOutput = if ([string]::IsNullOrWhiteSpace($Output)) {
        $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
        Resolve-RepoPath (Join-Path "backups" "m62-$Region-$stamp.bin")
    } else {
        Resolve-RepoPath $Output
    }

    if (Test-Path -LiteralPath $resolvedOutput -PathType Container) {
        throw "Backup output path is a directory: $resolvedOutput"
    }
    if ((Test-Path -LiteralPath $resolvedOutput -PathType Leaf) -and -not $Force) {
        throw "Refusing to overwrite existing backup file '$resolvedOutput'. Pass -Force to overwrite."
    }

    $blArgs = @(
        "--interface=uart",
        "--baudrate=$BaudRate",
        "--port=$normalizedPort",
        "--chipname=bl616",
        "--flash",
        "--read",
        "--start=$($regionSpec.Start)",
        "--len=$($regionSpec.Length)",
        "--file=$resolvedOutput"
    )
    $commandDisplay = "`"$blFlashExe`" $($blArgs -join ' ')"

    Write-Host "=== Flash Read-Only Backup Pre-Flight ==="
    Write-Host "  Target Board : $Board (chip=bl616)"
    Write-Host "  Region       : $Region (start=$($regionSpec.Start), len=$($regionSpec.Length), $($regionSpec.ExpectedBytes) bytes)"
    Write-Host "  Device       : $normalizedPort (baud=$BaudRate)"
    Write-Host "  Output       : $resolvedOutput"
    Write-Host "  Boundary     : Strict read-only BLFlashCommand.exe --flash --read (no erase or write). Requires BL616 ROM download mode."
    Write-Host "  Command      : $commandDisplay"

    if ($DryRun) {
        Write-Host "Dry-run backup validation passed. No device was opened and no flash was read."
        return 0
    }

    $outParent = Split-Path -Parent $resolvedOutput
    if ($outParent -and -not (Test-Path -LiteralPath $outParent)) {
        New-Item -ItemType Directory -Path $outParent -Force | Out-Null
    }

    $workDir = Join-Path $Script:RepoRoot "firmware"
    $readRes = Invoke-LoggedCommand -FilePath $blFlashExe -WorkingDirectory $workDir -Arguments $blArgs -StreamOutput
    if ($readRes.ExitCode -ne 0) {
        throw "BLFlashCommand.exe --flash --read failed with exit code $($readRes.ExitCode)."
    }
    if (-not (Test-Path -LiteralPath $resolvedOutput -PathType Leaf)) {
        throw "BLFlashCommand.exe exited cleanly but backup file was not created: $resolvedOutput"
    }

    $item = Get-Item -LiteralPath $resolvedOutput
    $sha = Get-FileSha256 $resolvedOutput
    Write-Host "Backup complete: $($item.Length) bytes, SHA-256 $sha -> $resolvedOutput"
    if ($item.Length -ne $regionSpec.ExpectedBytes) {
        throw "Backup size mismatch: expected $($regionSpec.ExpectedBytes) bytes, got $($item.Length) bytes."
    }

    if (($Region -eq "table" -or $Region -eq "full") -and $envInfo.Python) {
        $ptScript = Join-Paths $Script:RepoRoot @("tools", "bl616_pt_check.py")
        if (Test-Path -LiteralPath $ptScript -PathType Leaf) {
            Write-Host "=== Inspecting Partition Table ($resolvedOutput) ==="
            $ptRes = Invoke-LoggedCommand -FilePath $envInfo.Python.Executable `
                -Arguments @($ptScript, $resolvedOutput, "--offset", $regionSpec.Start) -StreamOutput
            if ($ptRes.ExitCode -ne 0) {
                throw "Partition table verification failed with exit code $($ptRes.ExitCode)."
            }
        }
    }

    return 0
}

function Invoke-CleanAction {
    $resolvedBuildDir = Resolve-RepoPath $BuildDir
    $protectedDirs = @(
        $Script:RepoRoot,
        (Join-Path $Script:RepoRoot "build"),
        (Join-Path $Script:RepoRoot "firmware"),
        (Join-Paths $Script:RepoRoot @("firmware", "build"))
    )
    foreach ($protected in $protectedDirs) {
        if ([string]::Equals($resolvedBuildDir, $protected, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to delete protected directory '$resolvedBuildDir'. Use a subdirectory such as 'build\windows'."
        }
    }

    if (Test-Path -LiteralPath $resolvedBuildDir) {
        Remove-Item -LiteralPath $resolvedBuildDir -Recurse -Force
        Write-Host "Removed Windows build directory: $resolvedBuildDir"
    } else {
        Write-Host "Windows build directory is already clean: $resolvedBuildDir"
    }
    return 0
}

function Show-Help {
    Write-Host @"
uni-usb-dongle Windows CLI (tools\windows.ps1)

Usage:
  .\tools\windows.ps1 doctor
  .\tools\windows.ps1 build [-Config Debug|Release] [-Target Host|Firmware|All] [-Board m62_12f]
  .\tools\windows.ps1 test  [-Suite All|C|Python|Web|Hardware] [-Name <pattern>] [-Config Debug|Release] [-Device <COMx>]
  .\tools\windows.ps1 flash [-Device <COMx|auto>] [-Firmware <path>] [-Method auto|hid|ota|uart] [-DryRun] [-Probe] [-ConfirmFlash]
  .\tools\windows.ps1 backup -Device <COMx> [-Region table|boot2|full] [-Output <path>] [-BaudRate 2000000] [-DryRun] [-Force]
  .\tools\windows.ps1 clean [-BuildDir build\windows]
"@
    return 0
}

try {
    $exitCode = switch ($Action) {
        "doctor" { Invoke-Doctor }
        "build"  { Invoke-BuildAction }
        "test"   { Invoke-TestAction }
        "flash"  { Invoke-FlashAction }
        "backup" { Invoke-BackupAction }
        "clean"  { Invoke-CleanAction }
        default  { Show-Help }
    }
    exit $exitCode
} catch {
    [Console]::Error.WriteLine("ERROR: $($_.Exception.Message)")
    exit 1
}
