[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$diagnosticVersion = 'V48'
$collectorRevision = 'V49-R2-crash-collector-hotfix-R2'
$gameRoot = [System.IO.Path]::GetFullPath($PSScriptRoot)
$timestamp = (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssZ')
$stagingRoot = Join-Path $env:TEMP ("KisakCOD-VR-Crash-Report-{0}-{1}" -f $timestamp, $PID)
$gameCrashDirectory = Join-Path $gameRoot 'CrashDumps'
$temporaryCrashDirectory = Join-Path $env:TEMP 'KisakCOD-VR-CrashDumps'
$crashDirectories = @($gameCrashDirectory, $temporaryCrashDirectory) | Select-Object -Unique

$desktopRoot = [Environment]::GetFolderPath('Desktop')
if ([string]::IsNullOrWhiteSpace($desktopRoot) -or -not (Test-Path -LiteralPath $desktopRoot)) {
    $desktopRoot = $gameRoot
}

$zipPath = Join-Path $desktopRoot ("KisakCOD-VR-Crash-Report-{0}-{1}.zip" -f $timestamp, $PID)

function Copy-DiagnosticFile {
    param(
        [Parameter(Mandatory = $true)]
        [string] $Source,

        [Parameter(Mandatory = $true)]
        [string] $DestinationName
    )

    if (Test-Path -LiteralPath $Source -PathType Leaf) {
        Copy-Item -LiteralPath $Source -Destination (Join-Path $stagingRoot $DestinationName) -Force
        return $true
    }

    return $false
}

function Add-CommandOutput {
    param(
        [Parameter(Mandatory = $true)]
        [AllowEmptyString()]
        [System.Collections.Generic.List[string]] $Lines,

        [Parameter(Mandatory = $true)]
        [string] $Title,

        [Parameter(Mandatory = $true)]
        [scriptblock] $Command
    )

    $Lines.Add('')
    $Lines.Add(('==== {0} ====' -f $Title))

    try {
        $output = & $Command 2>&1 | Out-String -Width 4096
        if ([string]::IsNullOrWhiteSpace($output)) {
            $Lines.Add('<no output>')
        }
        else {
            $Lines.Add($output.TrimEnd())
        }
    }
    catch {
        $Lines.Add(('ERROR: {0}' -f $_.Exception.Message))
    }
}

New-Item -ItemType Directory -Path $stagingRoot | Out-Null

try {
    $copied = [System.Collections.Generic.List[string]]::new()
    $missing = [System.Collections.Generic.List[string]]::new()

    $fixedFiles = @(
        @{ Source = (Join-Path $gameRoot 'OpenXR-Startup.log'); Name = 'OpenXR-Startup.log' },
        @{ Source = (Join-Path $gameRoot 'VR-Settings.bat'); Name = 'VR-Settings.bat' },
        @{ Source = (Join-Path $gameRoot 'main\console.log'); Name = 'console.log' }
    )

    foreach ($item in $fixedFiles) {
        if (Copy-DiagnosticFile -Source $item.Source -DestinationName $item.Name) {
            $copied.Add($item.Name)
        }
        else {
            $missing.Add($item.Source)
        }
    }

    foreach ($stateFileName in @('KisakCOD-VR-Last-Session.txt', 'LATEST.txt')) {
        $stateFile = @(
            foreach ($directory in $crashDirectories) {
                Get-Item -LiteralPath (Join-Path $directory $stateFileName) -ErrorAction SilentlyContinue
            }
        ) | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1

        if ($null -ne $stateFile -and (Copy-DiagnosticFile -Source $stateFile.FullName -DestinationName $stateFileName)) {
            $copied.Add($stateFileName)
        }
        else {
            $missing.Add(("{0} (searched: {1})" -f $stateFileName, ($crashDirectories -join '; ')))
        }
    }

    $recentReports = @(
        foreach ($directory in $crashDirectories) {
            Get-ChildItem -LiteralPath $directory -File -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^KisakCOD-VR-(crash|fatal)-.+\.txt$' } |
                ForEach-Object { $_ }
        }
    ) | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 2

    foreach ($report in $recentReports) {
        if (Copy-DiagnosticFile -Source $report.FullName -DestinationName $report.Name) {
            $copied.Add($report.Name)
        }

        $dumpPath = [System.IO.Path]::ChangeExtension($report.FullName, '.dmp')
        $dumpName = [System.IO.Path]::GetFileName($dumpPath)

        if (Copy-DiagnosticFile -Source $dumpPath -DestinationName $dumpName) {
            $copied.Add($dumpName)
        }
        else {
            $missing.Add($dumpPath)
        }
    }

    $summary = [System.Collections.Generic.List[string]]::new()
    $summary.Add('KISAK_SP_VR_CRASH_DIAGNOSTICS_V48')
    $summary.Add(('diagnostic_version={0}' -f $diagnosticVersion))
    $summary.Add(('collector_revision={0}' -f $collectorRevision))
    $summary.Add(('collection_utc={0}' -f (Get-Date).ToUniversalTime().ToString('o')))
    $summary.Add(('game_root={0}' -f $gameRoot))
    $summary.Add(('crash_directories_scanned={0}' -f ($crashDirectories -join '; ')))
    $summary.Add(('powershell_version={0}' -f $PSVersionTable.PSVersion.ToString()))
    $summary.Add(('windows_version={0}' -f [Environment]::OSVersion.VersionString))
    $summary.Add(('process_architecture={0}' -f $(if ([Environment]::Is64BitOperatingSystem) { '64-bit Windows; collector process may be 64-bit' } else { '32-bit Windows' })))

    $gameExecutable = Join-Path $gameRoot 'KisakCOD-sp.exe'
    if (Test-Path -LiteralPath $gameExecutable -PathType Leaf) {
        $gameFile = Get-Item -LiteralPath $gameExecutable
        $gameHash = Get-FileHash -LiteralPath $gameExecutable -Algorithm SHA256
        $summary.Add(('game_exe_size={0}' -f $gameFile.Length))
        $summary.Add(('game_exe_modified_utc={0}' -f $gameFile.LastWriteTimeUtc.ToString('o')))
        $summary.Add(('game_exe_sha256={0}' -f $gameHash.Hash.ToLowerInvariant()))
    }
    else {
        $summary.Add('game_exe=<missing>')
    }

    try {
        $operatingSystem = Get-CimInstance -ClassName Win32_OperatingSystem -ErrorAction Stop
        $summary.Add(('os_caption={0}' -f $operatingSystem.Caption))
        $summary.Add(('os_build={0}' -f $operatingSystem.BuildNumber))
        $physicalMemoryTotalBytes =
            ([UInt64] $operatingSystem.TotalVisibleMemorySize) * [UInt64] 1024
        $physicalMemoryFreeBytes =
            ([UInt64] $operatingSystem.FreePhysicalMemory) * [UInt64] 1024
        $summary.Add(('physical_memory_total_bytes={0}' -f $physicalMemoryTotalBytes))
        $summary.Add(('physical_memory_free_bytes={0}' -f $physicalMemoryFreeBytes))
    }
    catch {
        $summary.Add(('os_cim_error={0}' -f $_.Exception.Message))
    }

    try {
        $videoControllers = @(
            Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
                ForEach-Object { '{0} | driver {1} | RAM {2}' -f $_.Name, $_.DriverVersion, $_.AdapterRAM }
        )
        $summary.Add(('video_controllers={0}' -f ($videoControllers -join '; ')))
    }
    catch {
        $summary.Add(('video_controller_error={0}' -f $_.Exception.Message))
    }

    $runtimeVariables = @('XR_RUNTIME_JSON', 'XR_API_LAYER_PATH', 'XR_ENABLE_API_LAYERS')
    foreach ($variable in $runtimeVariables) {
        $value = [Environment]::GetEnvironmentVariable($variable)
        $summary.Add(('{0}={1}' -f $variable, $(if ([string]::IsNullOrWhiteSpace($value)) { '<unset>' } else { $value })))
    }

    Add-CommandOutput -Lines $summary -Title 'OpenXR active runtime - 32-bit HKLM' -Command {
        & reg.exe query 'HKLM\SOFTWARE\Khronos\OpenXR\1' /v ActiveRuntime /reg:32
    }
    Add-CommandOutput -Lines $summary -Title 'OpenXR active runtime - 64-bit HKLM' -Command {
        & reg.exe query 'HKLM\SOFTWARE\Khronos\OpenXR\1' /v ActiveRuntime /reg:64
    }
    Add-CommandOutput -Lines $summary -Title 'Relevant running processes' -Command {
        Get-Process -ErrorAction SilentlyContinue |
            Where-Object { $_.ProcessName -match '^(vrserver|vrmonitor|vrdashboard|OVRServer_x64|VirtualDesktop|PimaxClient|PimaxPlay)$' } |
            Select-Object -Property @('ProcessName', 'Id') |
            Format-Table -AutoSize
    }

    $summary.Add('')
    $summary.Add('==== Copied artifacts ====')
    if ($copied.Count -eq 0) {
        $summary.Add('<none>')
    }
    else {
        foreach ($name in ($copied | Sort-Object -Unique)) {
            $summary.Add($name)
        }
    }

    $summary.Add('')
    $summary.Add('==== Missing optional artifacts ====')
    if ($missing.Count -eq 0) {
        $summary.Add('<none>')
    }
    else {
        foreach ($name in ($missing | Sort-Object -Unique)) {
            $summary.Add($name)
        }
    }

    $summaryPath = Join-Path $stagingRoot 'System-and-Collection-Summary.txt'
    $summary | Set-Content -LiteralPath $summaryPath -Encoding UTF8

    @(
        'KisakCOD VR V48 crash diagnostic report',
        '',
        'Send this complete ZIP to JplayUltimate. The .dmp and matching .txt file are both required.',
        'The collector includes the latest two crash/fatal dumps, console.log, OpenXR-Startup.log,',
        'VR-Settings.bat, executable hash, OpenXR runtime registration, OS/GPU/memory summaries,',
        'and only the names/IDs of relevant running VR processes.',
        '',
        'It does not deliberately collect savegames, COD4 profile/config files, or browser data.',
        'The minidump contains limited game-process memory and can contain incidental strings.',
        'Share it privately. Logs and dumps can include absolute paths and the Windows account-folder name.'
    ) | Set-Content -LiteralPath (Join-Path $stagingRoot 'README-FIRST.txt') -Encoding UTF8

    if (Test-Path -LiteralPath $zipPath) {
        throw "Refusing to overwrite existing report: $zipPath"
    }

    Compress-Archive -Path (Join-Path $stagingRoot '*') -DestinationPath $zipPath -CompressionLevel Optimal

    Write-Host ''
    Write-Host 'KISAKCOD VR CRASH REPORT READY'
    Write-Host $zipPath
    Write-Host ''
    Write-Host 'Attach that complete ZIP to the bug report.'
}
finally {
    if (Test-Path -LiteralPath $stagingRoot) {
        Remove-Item -LiteralPath $stagingRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
