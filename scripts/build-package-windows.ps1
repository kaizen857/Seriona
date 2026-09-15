#requires -Version 5.1

[CmdletBinding()]
param(
    [ValidatePattern('^\d+\.\d+(\.\d+){0,2}$')]
    [string]$Version = '0.1',
    [string]$QtRoot,
    [string]$VcpkgRoot,
    [string]$VcpkgInstalledDir,
    [string]$CmakePath,
    [ValidateSet('Visual Studio 17 2022')]
    [string]$Generator = 'Visual Studio 17 2022',
    [AllowEmptyString()]
    [string]$BackendSourceDir,
    [string]$TagReaderSourceDir,
    [switch]$Force,
    [switch]$KeepBuild,
    [switch]$SkipTests,
    [switch]$Clean,
    [switch]$DryRun
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$DistRoot = Join-Path $RepoRoot 'dist'
$LogsRoot = Join-Path $DistRoot 'logs'
$PackageName = 'Seriona-windows-x64'
$PackageDirectory = Join-Path $DistRoot $PackageName
$PackageArchive = Join-Path $DistRoot ($PackageName + '.zip')
$BuildDirectory = Join-Path $DistRoot '.build\windows-x64-package'
$VcpkgStateRoot = Join-Path $DistRoot '.vcpkg'
$RunId = '{0}-{1}' -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $PID
$StagingParent = Join-Path $DistRoot ('.staging-' + $RunId)
$StagingDirectory = Join-Path $StagingParent $PackageName
$TemporaryArchive = Join-Path $DistRoot ('.' + $PackageName + '-' + $RunId + '.zip')
$SmokeDirectory = Join-Path $LogsRoot ('smoke-' + $RunId)
$VerifierPath = Join-Path $PSScriptRoot 'verify-windows-package.ps1'
$ManifestPath = Join-Path $RepoRoot 'vcpkg.json'

$QtRootWasExplicit = $PSBoundParameters.ContainsKey('QtRoot')
$VcpkgRootWasExplicit = $PSBoundParameters.ContainsKey('VcpkgRoot')
$BackendWasExplicit = $PSBoundParameters.ContainsKey('BackendSourceDir')
$TagReaderWasExplicit = $PSBoundParameters.ContainsKey('TagReaderSourceDir')
$CreatedBuildDirectory = $false
$CreatedStagingParent = $false
$Succeeded = $false
$Failure = $null

function Write-Phase {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ''
    Write-Host ('==> ' + $Message) -ForegroundColor Cyan
}

function Write-Detail {
    param([Parameter(Mandatory)][string]$Message)
    Write-Host ('    ' + $Message)
}

function Get-CommandPath {
    param([Parameter(Mandatory)][string]$Name)

    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $command) {
        return $null
    }
    if (-not [string]::IsNullOrWhiteSpace($command.Source)) {
        return $command.Source
    }
    return $command.Path
}

function Get-FullPath {
    param([Parameter(Mandatory)][string]$Path)

    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path (Get-Location).Path $Path))
}

function Test-Executable {
    param([Parameter(Mandatory)][string]$Path)

    return (Test-Path -LiteralPath $Path -PathType Leaf)
}

function Format-CommandLine {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $formatted = @($FilePath)
    foreach ($argument in $Arguments) {
        if ($argument -match '[\s"]') {
            $formatted += ('"' + $argument.Replace('"', '\"') + '"')
        } else {
            $formatted += $argument
        }
    }
    return ($formatted -join ' ')
}

function Invoke-CaptureNative {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$Description
    )

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $output = @(& $FilePath @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        $detail = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
        throw ('无法{0}（退出码 {1}）。{2}{3}' -f $Description, $exitCode, [Environment]::NewLine, $detail)
    }
    return @($output | ForEach-Object { $_.ToString() })
}

function Invoke-Native {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$Description,
        [Parameter(Mandatory)][string]$LogName
    )

    $commandLine = Format-CommandLine $FilePath $Arguments
    Write-Detail $commandLine
    if ($DryRun) {
        return
    }

    $logPath = Join-Path $LogsRoot ($RunId + '-' + $LogName + '.log')
    Set-Content -LiteralPath $logPath -Value ($commandLine + [Environment]::NewLine) -Encoding UTF8
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $FilePath @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousPreference
    }
    if ($exitCode -ne 0) {
        throw ('无法{0}（退出码 {1}）。完整输出：{2}' -f $Description, $exitCode, $logPath)
    }
}

function Get-VersionOrZero {
    param([Parameter(Mandatory)][string]$Text)

    $parsed = $null
    if ([version]::TryParse($Text, [ref]$parsed)) {
        return $parsed
    }
    return [version]'0.0'
}

function Assert-SafeOwnedPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string[]]$AllowedPaths
    )

    $fullPath = [IO.Path]::GetFullPath($Path).TrimEnd('\')
    foreach ($allowed in $AllowedPaths) {
        $fullAllowed = [IO.Path]::GetFullPath($allowed).TrimEnd('\')
        if ($fullPath.Equals($fullAllowed, [StringComparison]::OrdinalIgnoreCase)) {
            return
        }
    }
    throw ('拒绝删除未声明为本脚本所有的路径：' + $fullPath)
}

function Remove-OwnedPath {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string[]]$AllowedPaths
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return
    }
    Assert-SafeOwnedPath $Path $AllowedPaths
    Remove-Item -LiteralPath $Path -Recurse -Force
}

function Resolve-VisualStudio2022 {
    $vswhereCandidates = @()
    $vswhereOnPath = Get-CommandPath 'vswhere.exe'
    if (-not [string]::IsNullOrWhiteSpace($vswhereOnPath)) {
        $vswhereCandidates += $vswhereOnPath
    }
    $programFilesX86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $programFiles = [Environment]::GetEnvironmentVariable('ProgramFiles')
    if (-not [string]::IsNullOrWhiteSpace($programFilesX86)) {
        $vswhereCandidates += (Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe')
    }
    if (-not [string]::IsNullOrWhiteSpace($programFiles)) {
        $vswhereCandidates += (Join-Path $programFiles 'Microsoft Visual Studio\Installer\vswhere.exe')
    }

    $vswhere = $vswhereCandidates | Where-Object { Test-Executable $_ } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($vswhere)) {
        throw '未找到 vswhere.exe。请安装 Visual Studio 2022，并勾选“使用 C++ 的桌面开发”。'
    }

    $queryArguments = @(
        '-latest',
        '-products', '*',
        '-version', '[17.0,18.0)',
        '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
        '-property', 'installationPath'
    )
    $installationOutput = Invoke-CaptureNative $vswhere $queryArguments '查询 Visual Studio 2022 x64 C++ 工具'
    $installationPath = $installationOutput | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($installationPath)) {
        throw '未找到带 MSVC x64 工具的 Visual Studio 2022 实例。请安装“使用 C++ 的桌面开发”和 Windows SDK。'
    }
    $installationPath = [IO.Path]::GetFullPath($installationPath.Trim())
    $vsDevCmd = Join-Path $installationPath 'Common7\Tools\VsDevCmd.bat'
    $msbuild = Join-Path $installationPath 'MSBuild\Current\Bin\MSBuild.exe'
    if (-not (Test-Executable $vsDevCmd) -or -not (Test-Executable $msbuild)) {
        throw ('Visual Studio 2022 实例不完整：' + $installationPath)
    }

    $displayNameOutput = Invoke-CaptureNative $vswhere @('-path', $installationPath, '-property', 'displayName') '读取 Visual Studio 显示名称'
    $versionOutput = Invoke-CaptureNative $vswhere @('-path', $installationPath, '-property', 'installationVersion') '读取 Visual Studio 版本'
    return [PSCustomObject]@{
        InstallationPath = $installationPath
        VsDevCmd = $vsDevCmd
        DisplayName = (($displayNameOutput | Select-Object -First 1).Trim())
        Version = (($versionOutput | Select-Object -First 1).Trim())
    }
}

function Import-VisualStudioEnvironment {
    param([Parameter(Mandatory)]$VisualStudio)

    Write-Phase '导入 Visual Studio 2022 MSVC x64 环境'
    $command = 'call "{0}" -arch=x64 -host_arch=x64 >nul && set' -f $VisualStudio.VsDevCmd
    $environmentLines = @(& $env:ComSpec /d /s /c $command)
    if ($LASTEXITCODE -ne 0) {
        throw 'VsDevCmd.bat 未能初始化 x64 MSVC 环境。'
    }
    foreach ($line in $environmentLines) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) {
            continue
        }
        $name = $line.Substring(0, $separator)
        if ($name.StartsWith('=')) {
            continue
        }
        [Environment]::SetEnvironmentVariable($name, $line.Substring($separator + 1), 'Process')
    }

    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or $env:VSCMD_ARG_HOST_ARCH -ne 'x64') {
        throw 'Visual Studio 开发环境不是 host=x64、target=x64。'
    }
    $compiler = Get-CommandPath 'cl.exe'
    if ([string]::IsNullOrWhiteSpace($compiler)) {
        throw '已运行 VsDevCmd.bat，但 cl.exe 仍不可用。请修复 Visual Studio C++ 工作负载。'
    }
    $compilerPath = [IO.Path]::GetFullPath($compiler)
    $instancePrefix = $VisualStudio.InstallationPath.TrimEnd('\') + '\'
    if (-not $compilerPath.StartsWith($instancePrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw ('cl.exe 不属于选定的 Visual Studio 2022 实例：' + $compilerPath)
    }
    Write-Detail ($VisualStudio.DisplayName + ' ' + $VisualStudio.Version)
    Write-Detail ('cl.exe: ' + $compilerPath)
}

function Resolve-CMake {
    $cmake = $null
    if (-not [string]::IsNullOrWhiteSpace($CmakePath)) {
        if (-not (Test-Path -LiteralPath $CmakePath -PathType Leaf)) {
            throw ('指定的 CMake 路径不存在：' + $CmakePath)
        }
        $cmake = (Resolve-Path -LiteralPath $CmakePath).Path
        Write-Detail ('使用 -CmakePath 指定的 CMake：' + $cmake)
    }
    if ([string]::IsNullOrWhiteSpace($cmake)) {
        # 优先使用标准安装的 CMake（C:\Program Files\CMake），避免误取
        # 其它工具链（如 STM32CubeCLT）自带且位于 PATH 前部的 CMake。
        $standardCmake = Join-Path $env:ProgramFiles 'CMake\bin\cmake.exe'
        if (Test-Path -LiteralPath $standardCmake -PathType Leaf) {
            $cmake = $standardCmake
            Write-Detail ('使用标准安装的 CMake：' + $cmake)
        }
    }
    if ([string]::IsNullOrWhiteSpace($cmake)) {
        $cmake = Get-CommandPath 'cmake.exe'
        if ([string]::IsNullOrWhiteSpace($cmake)) {
            throw '未找到 CMake。请安装 CMake 3.20 或更高版本（或使用 -CmakePath 指定）。'
        }
    }
    $output = Invoke-CaptureNative $cmake @('--version') '查询 CMake 版本'
    $firstLine = $output | Select-Object -First 1
    if ($firstLine -notmatch 'cmake version\s+([0-9]+(?:\.[0-9]+){1,3})') {
        throw ('无法解析 CMake 版本：' + $firstLine)
    }
    $version = [version]$Matches[1]
    if ($version -lt [version]'3.20') {
        throw ('CMake 版本过低：{0}；完整后端依赖链要求 3.20 或更高版本。' -f $version)
    }
    return [PSCustomObject]@{ Path = $cmake; Version = $version.ToString() }
}

function Resolve-Python3 {
    $candidates = @()
    foreach ($commandName in @('python.exe', 'python3.exe')) {
        $commandPath = Get-CommandPath $commandName
        if (-not [string]::IsNullOrWhiteSpace($commandPath)) {
            $candidates += [PSCustomObject]@{
                Launcher = $commandPath
                PrefixArguments = @()
            }
        }
    }
    $pyLauncher = Get-CommandPath 'py.exe'
    if (-not [string]::IsNullOrWhiteSpace($pyLauncher)) {
        $candidates += [PSCustomObject]@{
            Launcher = $pyLauncher
            PrefixArguments = @('-3')
        }
    }

    $seen = @{}
    $errors = @()
    foreach ($candidate in $candidates) {
        $key = $candidate.Launcher.ToLowerInvariant() + '|' + ($candidate.PrefixArguments -join ' ')
        if ($seen.ContainsKey($key)) {
            continue
        }
        $seen[$key] = $true
        try {
            $probeArguments = @($candidate.PrefixArguments) + @(
                '-c',
                "import sys; print(sys.executable); print('.'.join(map(str, sys.version_info[:3])))"
            )
            $output = Invoke-CaptureNative $candidate.Launcher $probeArguments '验证 Python 3'
            if ($output.Count -lt 2) {
                throw '版本探测未返回解释器路径和版本。'
            }
            $executablePath = [IO.Path]::GetFullPath($output[$output.Count - 2].Trim())
            $versionText = $output[$output.Count - 1].Trim()
            $version = Get-VersionOrZero $versionText
            if ($version.Major -ne 3) {
                throw ('检测到的不是 Python 3：' + $versionText)
            }
            if (-not (Test-Executable $executablePath)) {
                throw ('Python 3 报告的解释器路径不存在：' + $executablePath)
            }
            return [PSCustomObject]@{
                Path = $executablePath
                Version = $versionText
            }
        } catch {
            $errors += ($candidate.Launcher + ': ' + $_.Exception.Message)
        }
    }

    $detail = if ($errors.Count -gt 0) { [Environment]::NewLine + ($errors -join [Environment]::NewLine) } else { '' }
    throw ('未找到可用的 Python 3。TagReader 安全样本测试在配置期要求 Python 3；请安装 Python 3 并将 python.exe 或 py.exe 加入 PATH。' + $detail)
}

function Convert-ToQtPrefix {
    param([Parameter(Mandatory)][string]$Candidate)

    $prefix = Get-FullPath $Candidate
    if ((Split-Path $prefix -Leaf) -ieq 'bin') {
        return (Split-Path $prefix -Parent)
    }
    if ((Split-Path $prefix -Leaf) -ieq 'Qt6' -and
        (Split-Path (Split-Path $prefix -Parent) -Leaf) -ieq 'cmake') {
        return (Split-Path (Split-Path (Split-Path $prefix -Parent) -Parent) -Parent)
    }
    return $prefix
}

function Test-QtCandidate {
    param([Parameter(Mandatory)][string]$Candidate)

    $prefix = Convert-ToQtPrefix $Candidate
    $qmake = Join-Path $prefix 'bin\qmake.exe'
    $windeployqt = Join-Path $prefix 'bin\windeployqt.exe'
    $qt6Dir = Join-Path $prefix 'lib\cmake\Qt6'
    $qtConfig = Join-Path $qt6Dir 'Qt6Config.cmake'
    $qconfigPri = Join-Path $prefix 'mkspecs\qconfig.pri'
    if (-not (Test-Executable $qmake) -or -not (Test-Executable $windeployqt) -or
        -not (Test-Path -LiteralPath $qtConfig -PathType Leaf) -or
        -not (Test-Path -LiteralPath $qconfigPri -PathType Leaf)) {
        throw ('缺少 qmake.exe、windeployqt.exe、Qt6Config.cmake 或 qconfig.pri：' + $prefix)
    }

    $versionText = ((Invoke-CaptureNative $qmake @('-query', 'QT_VERSION') '查询 Qt 版本') | Select-Object -First 1).Trim()
    $spec = ((Invoke-CaptureNative $qmake @('-query', 'QMAKE_XSPEC') '查询 Qt 编译器规格') | Select-Object -First 1).Trim()
    $version = Get-VersionOrZero $versionText
    if ($version -lt [version]'6.8') {
        throw ('Qt 版本过低：' + $versionText)
    }
    if ($spec -ne 'win32-msvc') {
        throw ('不是 MSVC Qt kit（QMAKE_XSPEC=' + $spec + '）：' + $prefix)
    }
    $architectureLine = Get-Content -LiteralPath $qconfigPri | Where-Object { $_ -match '^QT_ARCH\s*=\s*(.+)$' } | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($architectureLine) -or $architectureLine -notmatch '^QT_ARCH\s*=\s*x86_64\s*$') {
        throw ('不是 x64 Qt kit（期望 QT_ARCH=x86_64）：' + $prefix)
    }

    return [PSCustomObject]@{
        Prefix = $prefix
        Qt6Dir = $qt6Dir
        QMake = $qmake
        WindeployQt = $windeployqt
        Version = $versionText
        Spec = $spec
        Architecture = 'x86_64'
    }
}

function Resolve-QtInstallation {
    $candidates = @()
    if ($QtRootWasExplicit) {
        $candidates += $QtRoot
    } else {
        foreach ($environmentCandidate in @($env:Qt6_ROOT, $env:Qt6_DIR, $env:QTDIR)) {
            if (-not [string]::IsNullOrWhiteSpace($environmentCandidate)) {
                $candidates += $environmentCandidate
            }
        }
        if (-not [string]::IsNullOrWhiteSpace($env:CMAKE_PREFIX_PATH)) {
            $candidates += @($env:CMAKE_PREFIX_PATH.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        }
        foreach ($toolName in @('qmake.exe', 'windeployqt.exe')) {
            $toolPath = Get-CommandPath $toolName
            if (-not [string]::IsNullOrWhiteSpace($toolPath)) {
                $candidates += (Split-Path (Split-Path $toolPath -Parent) -Parent)
            }
        }
        if (Test-Path -LiteralPath 'C:\Qt' -PathType Container) {
            $versionDirectories = Get-ChildItem -LiteralPath 'C:\Qt' -Directory -ErrorAction SilentlyContinue |
                Sort-Object @{ Expression = { Get-VersionOrZero $_.Name }; Descending = $true }
            foreach ($versionDirectory in $versionDirectories) {
                $candidates += (Join-Path $versionDirectory.FullName 'msvc2022_64')
            }
        }
    }

    $seen = @{}
    $errors = @()
    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        try {
            $normalized = Convert-ToQtPrefix $candidate
            if ($seen.ContainsKey($normalized.ToLowerInvariant())) {
                continue
            }
            $seen[$normalized.ToLowerInvariant()] = $true
            return (Test-QtCandidate $normalized)
        } catch {
            $errors += $_.Exception.Message
            if ($QtRootWasExplicit) {
                throw ('-QtRoot 无效：' + $_.Exception.Message)
            }
        }
    }

    $detail = if ($errors.Count -gt 0) { [Environment]::NewLine + ($errors -join [Environment]::NewLine) } else { '' }
    throw ('未找到 Qt 6.8+ MSVC 2022 x64 kit。请安装 msvc2022_64 kit，或传入 -QtRoot。' + $detail)
}

function Resolve-VcpkgInstallation {
    $rootCandidates = @()
    if ($VcpkgRootWasExplicit) {
        $rootCandidates += $VcpkgRoot
    } else {
        if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
            $rootCandidates += $env:VCPKG_ROOT
        }
        $vcpkgOnPath = Get-CommandPath 'vcpkg.exe'
        if (-not [string]::IsNullOrWhiteSpace($vcpkgOnPath)) {
            $rootCandidates += (Split-Path $vcpkgOnPath -Parent)
        }
        $rootCandidates += 'C:\vcpkg'
        if (-not [string]::IsNullOrWhiteSpace($VcpkgInstalledDir)) {
            $installedCandidate = Get-FullPath $VcpkgInstalledDir
            if ((Split-Path $installedCandidate -Leaf) -ieq 'x64-windows') {
                $rootCandidates += (Split-Path (Split-Path $installedCandidate -Parent) -Parent)
            }
        }
    }

    $selectedRoot = $null
    foreach ($candidate in $rootCandidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        $root = Get-FullPath $candidate
        if ((Test-Executable (Join-Path $root 'vcpkg.exe')) -and
            (Test-Path -LiteralPath (Join-Path $root 'scripts\buildsystems\vcpkg.cmake') -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $root 'scripts\buildsystems\msbuild\applocal.ps1') -PathType Leaf)) {
            $selectedRoot = $root
            break
        }
        if ($VcpkgRootWasExplicit) {
            throw ('-VcpkgRoot 不是完整的 vcpkg 根目录：' + $root)
        }
    }
    if ([string]::IsNullOrWhiteSpace($selectedRoot)) {
        throw '未找到 vcpkg。请先克隆并 bootstrap vcpkg，然后传入 -VcpkgRoot 或设置 VCPKG_ROOT。'
    }

    $installedRoot = Join-Path $DistRoot '.vcpkg_installed'
    if (-not [string]::IsNullOrWhiteSpace($VcpkgInstalledDir)) {
        $installedRoot = Get-FullPath $VcpkgInstalledDir
        if ((Split-Path $installedRoot -Leaf) -ieq 'x64-windows') {
            $installedRoot = Split-Path $installedRoot -Parent
        }
    }
    return [PSCustomObject]@{
        Root = $selectedRoot
        Executable = Join-Path $selectedRoot 'vcpkg.exe'
        Toolchain = Join-Path $selectedRoot 'scripts\buildsystems\vcpkg.cmake'
        AppLocal = Join-Path $selectedRoot 'scripts\buildsystems\msbuild\applocal.ps1'
        InstalledRoot = $installedRoot
        TripletRoot = Join-Path $installedRoot 'x64-windows'
        TripletBin = Join-Path $installedRoot 'x64-windows\bin'
        PkgConfigDir = Join-Path $installedRoot 'x64-windows\lib\pkgconfig'
        PkgConfigExecutable = Join-Path $installedRoot 'x64-windows\tools\pkgconf\pkgconf.exe'
    }
}

function Resolve-LocalSource {
    param(
        [Parameter(Mandatory)][string]$Name,
        [string]$ExplicitPath,
        [Parameter(Mandatory)][bool]$WasExplicit,
        [Parameter(Mandatory)][string[]]$Candidates
    )

    if ($WasExplicit) {
        if ([string]::IsNullOrWhiteSpace($ExplicitPath)) {
            return $null
        }
        $resolved = Get-FullPath $ExplicitPath
        if (-not (Test-Path -LiteralPath (Join-Path $resolved 'CMakeLists.txt') -PathType Leaf)) {
            throw ('-{0}SourceDir 不含 CMakeLists.txt：{1}' -f $Name, $resolved)
        }
        return $resolved
    }
    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        $resolved = Get-FullPath $candidate
        if (Test-Path -LiteralPath (Join-Path $resolved 'CMakeLists.txt') -PathType Leaf) {
            return $resolved
        }
    }
    return $null
}

function Assert-RestoredVcpkgLayout {
    param([Parameter(Mandatory)]$Vcpkg)

    foreach ($requiredPath in @($Vcpkg.TripletBin, $Vcpkg.PkgConfigDir, $Vcpkg.PkgConfigExecutable)) {
        if (-not (Test-Path -LiteralPath $requiredPath)) {
            throw ('vcpkg manifest 恢复后缺少：' + $requiredPath)
        }
    }
    foreach ($pcFile in @('libavformat.pc', 'libavcodec.pc', 'libavutil.pc', 'libavfilter.pc', 'libswresample.pc', 'libswscale.pc', 'libxxhash.pc')) {
        if (-not (Test-Path -LiteralPath (Join-Path $Vcpkg.PkgConfigDir $pcFile) -PathType Leaf)) {
            throw ('vcpkg release pkg-config 目录缺少：' + $pcFile)
        }
    }
}

function Find-ReleaseExecutable {
    $candidates = @(
        (Join-Path $BuildDirectory 'Release\seriona.exe'),
        (Join-Path $BuildDirectory 'seriona.exe')
    )
    foreach ($candidate in $candidates) {
        if (Test-Executable $candidate) {
            return $candidate
        }
    }
    throw ('Release 构建未生成 seriona.exe。已检查：' + ($candidates -join ', '))
}

function Get-GitRevision {
    $git = Get-CommandPath 'git.exe'
    if ([string]::IsNullOrWhiteSpace($git)) {
        return 'unavailable'
    }
    try {
        $revision = Invoke-CaptureNative $git @('-C', $RepoRoot, 'rev-parse', '--short=12', 'HEAD') '读取 Git 版本'
        return (($revision | Select-Object -First 1).Trim())
    } catch {
        return 'unavailable'
    }
}

function Write-PackageDocuments {
    param(
        [Parameter(Mandatory)]$VisualStudio,
        [Parameter(Mandatory)]$CMake,
        [Parameter(Mandatory)]$Python,
        [Parameter(Mandatory)]$Qt,
        [Parameter(Mandatory)]$Vcpkg,
        [string]$Backend,
        [string]$TagReader,
        [Parameter(Mandatory)][bool]$MockOnly
    )

    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $vcpkgVersion = ((Invoke-CaptureNative $Vcpkg.Executable @('version') '查询 vcpkg 版本') | Select-Object -First 1).Trim()
    $backendDescription = if ($MockOnly) { 'mock-only（显式禁用后端）' } elseif ($null -ne $Backend) { $Backend } else { 'FetchContent: Seriona_Backend main' }
    $tagReaderDescription = if ($MockOnly) { 'not used' } elseif ($null -ne $TagReader) { $TagReader } else { '由 Seriona_Backend 本地发现或 FetchContent' }
    $testDescription = if ($SkipTests) { '跳过 CTest；已执行全部包 Smoke 场景' } else { 'CTest Release + 全部包 Smoke 场景' }
    $buildInfo = @(
        'Seriona Windows x64 Release package',
        ('Built-UTC: ' + [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')),
        ('Git-Revision: ' + (Get-GitRevision)),
        ('Generator: ' + $Generator),
        'Architecture: x64',
        'Toolset: v143,host=x64',
        ('Visual-Studio: ' + $VisualStudio.DisplayName + ' ' + $VisualStudio.Version),
        ('CMake: ' + $CMake.Version),
        ('Python: ' + $Python.Version + ' (' + $Python.Path + ')'),
        ('Qt: ' + $Qt.Version + ' ' + $Qt.Spec + ' x86_64'),
        ('vcpkg: ' + $vcpkgVersion),
        ('vcpkg-builtin-baseline: ' + $manifest.'builtin-baseline'),
        'vcpkg-triplet: x64-windows',
        ('Backend: ' + $backendDescription),
        ('TagReader: ' + $tagReaderDescription),
        ('Verification: ' + $testDescription)
    )
    Set-Content -LiteralPath (Join-Path $StagingDirectory 'BUILD-INFO.txt') -Value $buildInfo -Encoding UTF8

    $packageReadme = @(
        'Seriona Windows x64',
        '',
        '双击 seriona.exe 启动。请保持本目录中的 DLL、plugins 和 qml 子目录完整。',
        '程序数据、数据库、日志、封面缓存和设置保存在程序旁的 SerionaData 目录。',
        '首次启动后如需搬移程序，请先退出 Seriona，再整体移动此目录。',
        '',
        ('构建模式：' + $(if ($MockOnly) { 'Release mock-only（无真实扫描/播放能力）' } else { 'Release（含 Seriona_Backend）' })),
        '许可证见 LICENSE，详细构建信息见 BUILD-INFO.txt。'
    )
    Set-Content -LiteralPath (Join-Path $StagingDirectory 'README.txt') -Value $packageReadme -Encoding UTF8
    Copy-Item -LiteralPath (Join-Path $RepoRoot 'LICENSE') -Destination (Join-Path $StagingDirectory 'LICENSE')
}

function Publish-VerifiedPackage {
    $backupDirectory = Join-Path $DistRoot ('.backup-directory-' + $RunId)
    $backupArchive = Join-Path $DistRoot ('.backup-archive-' + $RunId + '.zip')
    $movedNewDirectory = $false
    $movedNewArchive = $false
    try {
        if (Test-Path -LiteralPath $PackageDirectory) {
            Move-Item -LiteralPath $PackageDirectory -Destination $backupDirectory
        }
        if (Test-Path -LiteralPath $PackageArchive) {
            Move-Item -LiteralPath $PackageArchive -Destination $backupArchive
        }
        Move-Item -LiteralPath $StagingDirectory -Destination $PackageDirectory
        $movedNewDirectory = $true
        Move-Item -LiteralPath $TemporaryArchive -Destination $PackageArchive
        $movedNewArchive = $true

        if (Test-Path -LiteralPath $backupDirectory) {
            Remove-Item -LiteralPath $backupDirectory -Recurse -Force -ErrorAction SilentlyContinue
        }
        if (Test-Path -LiteralPath $backupArchive) {
            Remove-Item -LiteralPath $backupArchive -Force -ErrorAction SilentlyContinue
        }
    } catch {
        if ($movedNewArchive -and (Test-Path -LiteralPath $PackageArchive)) {
            Remove-OwnedPath $PackageArchive @($PackageArchive)
        }
        if ($movedNewDirectory -and (Test-Path -LiteralPath $PackageDirectory)) {
            Remove-OwnedPath $PackageDirectory @($PackageDirectory)
        }
        if (Test-Path -LiteralPath $backupDirectory) {
            Move-Item -LiteralPath $backupDirectory -Destination $PackageDirectory
        }
        if (Test-Path -LiteralPath $backupArchive) {
            Move-Item -LiteralPath $backupArchive -Destination $PackageArchive
        }
        throw
    }
}

try {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw ('缺少 vcpkg manifest：' + $ManifestPath)
    }
    if (-not (Test-Path -LiteralPath $VerifierPath -PathType Leaf)) {
        throw ('缺少包验证脚本：' + $VerifierPath)
    }
    if (-not $DryRun -and
        ((Test-Path -LiteralPath $PackageDirectory) -or (Test-Path -LiteralPath $PackageArchive)) -and
        -not $Force) {
        throw ('输出已存在：{0} 或 {1}。确认替换时使用 -Force。' -f $PackageDirectory, $PackageArchive)
    }

    if (-not $DryRun) {
        New-Item -ItemType Directory -Path $LogsRoot -Force | Out-Null
        foreach ($stateDirectory in @('archives', 'buildtrees', 'downloads', 'packages')) {
            New-Item -ItemType Directory -Path (Join-Path $VcpkgStateRoot $stateDirectory) -Force | Out-Null
        }
    }

    Write-Phase '发现并验证 Windows x64 构建工具'
    $visualStudio = Resolve-VisualStudio2022
    Import-VisualStudioEnvironment $visualStudio
    $cmake = Resolve-CMake
    $python = Resolve-Python3
    $qt = Resolve-QtInstallation
    $vcpkg = Resolve-VcpkgInstallation
    Write-Detail ('CMake: ' + $cmake.Path + ' (' + $cmake.Version + ')')
    Write-Detail ('Python: ' + $python.Path + ' (' + $python.Version + ')')
    Write-Detail ('Qt: ' + $qt.Prefix + ' (' + $qt.Version + ', ' + $qt.Spec + ', x86_64)')
    Write-Detail ('vcpkg: ' + $vcpkg.Root)
    Write-Detail ('manifest install root: ' + $vcpkg.InstalledRoot)
    Write-Detail ('vcpkg state root: ' + $VcpkgStateRoot)

    $backend = Resolve-LocalSource 'Backend' $BackendSourceDir $BackendWasExplicit @((Join-Path $RepoRoot '..\Seriona_Backend'))
    $mockOnly = $BackendWasExplicit -and [string]::IsNullOrWhiteSpace($BackendSourceDir)
    if ($mockOnly -and $TagReaderWasExplicit) {
        throw 'mock-only 构建不使用 TagReader；请移除 -TagReaderSourceDir。'
    }
    $tagReaderCandidates = @((Join-Path $RepoRoot '..\TagReader'))
    if ($null -ne $backend) {
        $tagReaderCandidates += (Join-Path $backend '..\TagReader')
    }
    $tagReader = if ($mockOnly) { $null } else { Resolve-LocalSource 'TagReader' $TagReaderSourceDir $TagReaderWasExplicit $tagReaderCandidates }
    if ($mockOnly) {
        Write-Detail 'Backend: 显式禁用（mock-only）'
        Write-Detail 'TagReader: 不使用'
    } else {
        Write-Detail ('Backend: ' + $(if ($null -ne $backend) { '本地 ' + $backend } else { '未找到本地源码，CMake 将 FetchContent main' }))
        Write-Detail ('TagReader: ' + $(if ($null -ne $tagReader) { '本地 ' + $tagReader } else { '由 Backend 本地发现或 FetchContent' }))
    }

    if ($Clean) {
        Write-Phase '清理本工作流拥有的构建目录'
        if ($DryRun) {
            Write-Detail ('将删除：' + $BuildDirectory)
        } else {
            Remove-OwnedPath $BuildDirectory @($BuildDirectory)
        }
    }

    Write-Phase '恢复 vcpkg manifest 依赖（x64-windows 动态 triplet）'
    $restoreArguments = @(
        'install',
        '--triplet=x64-windows',
        ('--x-manifest-root=' + $RepoRoot),
        ('--x-install-root=' + $vcpkg.InstalledRoot),
        ('--x-buildtrees-root=' + (Join-Path $VcpkgStateRoot 'buildtrees')),
        ('--x-packages-root=' + (Join-Path $VcpkgStateRoot 'packages')),
        ('--downloads-root=' + (Join-Path $VcpkgStateRoot 'downloads')),
        '--disable-metrics'
    )
    $oldBinaryCache = $env:VCPKG_DEFAULT_BINARY_CACHE
    try {
        $env:VCPKG_DEFAULT_BINARY_CACHE = Join-Path $VcpkgStateRoot 'archives'
        Invoke-Native $vcpkg.Executable $restoreArguments '恢复 vcpkg manifest 依赖' '01-vcpkg-restore'
    } finally {
        $env:VCPKG_DEFAULT_BINARY_CACHE = $oldBinaryCache
    }

    if ($DryRun) {
        Write-Phase 'DryRun 构建、部署与验证计划'
        Write-Detail ('生成器：' + $Generator + '；-A x64；-T v143,host=x64')
        Write-Detail ('构建目录：' + $BuildDirectory)
        Write-Detail ('配置/构建：cmake configure；cmake --build --config Release')
        if (-not $SkipTests) {
            Write-Detail '测试：ctest --build-config Release --output-on-failure --timeout 900'
        }
        Write-Detail ('部署：windeployqt --release --qmldir ' + (Join-Path $RepoRoot 'qml'))
        Write-Detail '依赖部署：vcpkg applocal.ps1（仅递归复制 seriona.exe 的动态依赖）'
        Write-Detail '验证：startup、main-playback、lyrics、sidebar-tree、settings-menu、empty-library'
        Write-Detail ('最终目录：' + $PackageDirectory)
        Write-Detail ('最终 ZIP：' + $PackageArchive)
        $Succeeded = $true
        return
    }

    Assert-RestoredVcpkgLayout $vcpkg
    New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
    $CreatedBuildDirectory = $true

    $configureArguments = @(
        '-S', $RepoRoot,
        '-B', $BuildDirectory,
        '-G', $Generator,
        '-A', 'x64',
        '-T', 'v143,host=x64',
        ('-DCMAKE_GENERATOR_INSTANCE=' + $visualStudio.InstallationPath),
        ('-DCMAKE_TOOLCHAIN_FILE=' + $vcpkg.Toolchain),
        '-DVCPKG_TARGET_TRIPLET=x64-windows',
        '-DVCPKG_HOST_TRIPLET=x64-windows',
        '-DVCPKG_MANIFEST_MODE=ON',
        '-DVCPKG_MANIFEST_INSTALL=OFF',
        ('-DVCPKG_MANIFEST_DIR=' + $RepoRoot),
        ('-DVCPKG_INSTALLED_DIR=' + $vcpkg.InstalledRoot),
        ('-DPKG_CONFIG_EXECUTABLE=' + $vcpkg.PkgConfigExecutable),
        ('-DPython3_EXECUTABLE=' + $python.Path),
        ('-DQt6_DIR=' + $qt.Qt6Dir),
        ('-DSERIONA_VERSION=' + $Version),
        ('-DBUILD_TESTING=' + $(if ($SkipTests) { 'OFF' } else { 'ON' }))
    )
    if ($mockOnly) {
        $configureArguments += '-DSERIONA_BACKEND_SOURCE_DIR='
    } elseif ($null -ne $backend) {
        $configureArguments += ('-DSERIONA_BACKEND_SOURCE_DIR=' + $backend)
    }
    if ($null -ne $tagReader) {
        $configureArguments += ('-DSERIONA_TAGREADER_SOURCE_DIR=' + $tagReader)
    }

    $oldPkgConfigLibDir = $env:PKG_CONFIG_LIBDIR
    $oldPkgConfigPath = $env:PKG_CONFIG_PATH
    try {
        $env:PKG_CONFIG_LIBDIR = $vcpkg.PkgConfigDir
        $env:PKG_CONFIG_PATH = ''
        Write-Phase '配置 Visual Studio 2022 x64 多配置构建'
        Invoke-Native $cmake.Path $configureArguments '配置 Visual Studio Release 构建' '02-cmake-configure'

        Write-Phase '构建 Release 目标'
        Invoke-Native $cmake.Path @('--build', $BuildDirectory, '--config', 'Release', '--parallel') '构建 Release 目标' '03-cmake-build'

        if (-not $SkipTests) {
            Write-Phase '运行 Release CTest'
            $ctest = Join-Path (Split-Path $cmake.Path -Parent) 'ctest.exe'
            if (-not (Test-Executable $ctest)) {
                $ctest = Get-CommandPath 'ctest.exe'
            }
            if ([string]::IsNullOrWhiteSpace($ctest)) {
                throw '未找到与 CMake 配套的 ctest.exe。'
            }
            $oldPath = $env:PATH
            $oldForceStderrLogging = $env:QT_FORCE_STDERR_LOGGING
            try {
                $env:PATH = (Join-Path $qt.Prefix 'bin') + ';' + $vcpkg.TripletBin + ';' + (Join-Path $BuildDirectory 'Release') + ';' + $oldPath
                # Windows 无控制台时 QTest 经 OutputDebugString 输出、ctest 捕获不到失败详情：强制 Qt 日志走 stderr。
                $env:QT_FORCE_STDERR_LOGGING = '1'
                Invoke-Native $ctest @('--test-dir', $BuildDirectory, '--build-config', 'Release', '--output-on-failure', '--timeout', '900') '运行 Release CTest' '04-ctest'
            } finally {
                $env:QT_FORCE_STDERR_LOGGING = $oldForceStderrLogging
                $env:PATH = $oldPath
            }
        }
    } finally {
        $env:PKG_CONFIG_LIBDIR = $oldPkgConfigLibDir
        $env:PKG_CONFIG_PATH = $oldPkgConfigPath
    }

    $builtExecutable = Find-ReleaseExecutable
    Write-Phase '准备隔离的暂存包'
    New-Item -ItemType Directory -Path $StagingDirectory -Force | Out-Null
    $CreatedStagingParent = $true
    $stagedExecutable = Join-Path $StagingDirectory 'seriona.exe'
    Copy-Item -LiteralPath $builtExecutable -Destination $stagedExecutable

    Write-Phase '使用 windeployqt 部署 Qt DLL、插件和 QML imports'
    Invoke-Native $qt.WindeployQt @(
        '--release',
        '--force',
        '--include-plugins', 'qoffscreen',
        '--qmldir', (Join-Path $RepoRoot 'qml'),
        '--dir', $StagingDirectory,
        $stagedExecutable
    ) '部署 Qt 运行时' '05-windeployqt'

    Write-Phase '使用 vcpkg applocal.ps1 部署应用实际依赖的 DLL'
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    if (-not (Test-Executable $windowsPowerShell)) {
        $windowsPowerShell = Get-CommandPath 'powershell.exe'
    }
    if ([string]::IsNullOrWhiteSpace($windowsPowerShell)) {
        throw '未找到 Windows PowerShell 5.1；vcpkg applocal.ps1 和包验证需要它。'
    }
    Invoke-Native $windowsPowerShell @(
        '-NoLogo',
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $vcpkg.AppLocal,
        '-targetBinary', $stagedExecutable,
        '-installedDir', $vcpkg.TripletBin,
        '-copiedFilesLog', (Join-Path $LogsRoot ($RunId + '-applocal-files.txt'))
    ) '部署 vcpkg 动态依赖' '06-vcpkg-applocal'

    Write-PackageDocuments $visualStudio $cmake $python $qt $vcpkg $backend $tagReader $mockOnly

    Write-Phase '在受限 PATH 下验证包结构和全部 Smoke 场景'
    $verifyArguments = @(
        '-NoLogo',
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $VerifierPath,
        '-PackageDirectory', $StagingDirectory,
        '-SmokeOutputDirectory', $SmokeDirectory
    )
    if ($mockOnly) {
        $verifyArguments += '-MockOnly'
    }
    Invoke-Native $windowsPowerShell $verifyArguments '验证 Windows 包' '07-package-verify'

    Write-Phase '创建暂存 ZIP'
    Compress-Archive -Path $StagingDirectory -DestinationPath $TemporaryArchive -CompressionLevel Optimal
    if (-not (Test-Path -LiteralPath $TemporaryArchive -PathType Leaf) -or (Get-Item -LiteralPath $TemporaryArchive).Length -eq 0) {
        throw ('ZIP 创建失败或为空：' + $TemporaryArchive)
    }

    Write-Phase '发布已验证的目录和 ZIP'
    Publish-VerifiedPackage
    $Succeeded = $true
    Write-Host ''
    Write-Host ('构建完成：' + $PackageDirectory) -ForegroundColor Green
    Write-Host ('ZIP：' + $PackageArchive) -ForegroundColor Green
    Write-Host ('日志：' + $LogsRoot)
} catch {
    $Failure = $_
} finally {
    if (Test-Path -LiteralPath $TemporaryArchive) {
        Remove-OwnedPath $TemporaryArchive @($TemporaryArchive)
    }
    if (-not $KeepBuild) {
        if ($CreatedStagingParent -and (Test-Path -LiteralPath $StagingParent)) {
            Remove-OwnedPath $StagingParent @($StagingParent)
        }
        if ($CreatedBuildDirectory -and (Test-Path -LiteralPath $BuildDirectory)) {
            Remove-OwnedPath $BuildDirectory @($BuildDirectory)
        }
    } elseif (-not $DryRun) {
        if (Test-Path -LiteralPath $BuildDirectory) {
            Write-Detail ('保留构建目录：' + $BuildDirectory)
        }
        if (Test-Path -LiteralPath $StagingParent) {
            Write-Detail ('保留暂存目录：' + $StagingParent)
        }
    }
}

if ($null -ne $Failure) {
    Write-Host ''
    Write-Host ('构建失败：' + $Failure.Exception.Message) -ForegroundColor Red
    if (Test-Path -LiteralPath $LogsRoot) {
        Write-Host ('日志保留在：' + $LogsRoot) -ForegroundColor Yellow
    }
    exit 1
}
if (-not $Succeeded) {
    exit 1
}
