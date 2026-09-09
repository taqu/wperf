param([Parameter(Mandatory=$true)][string]$Executable, [switch]$Resources, [switch]$Deep)
$ErrorActionPreference = 'Stop'
function Check($condition, $message) { if (-not $condition) { throw $message } }
function Run-Cli([string]$arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = $arguments
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = New-Object System.Text.UTF8Encoding
    $info.StandardErrorEncoding = New-Object System.Text.UTF8Encoding
    $process = [System.Diagnostics.Process]::Start($info)
    try {
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        Check ($process.WaitForExit(20000)) 'CLI did not exit within 20 seconds'
        return @{ Code = $process.ExitCode; Out = $stdout.Result; Err = $stderr.Result }
    } finally { $process.Dispose() }
}
if ($Deep) { $Resources = $true }
if (-not $Resources) {
    $result = Run-Cli '--help'
    Check ($result.Code -eq 0 -and $result.Out.Contains('--lock') -and $result.Err -eq '') 'help'
    foreach ($arguments in @('--lock', '--unknown-option', '--json', '--deep', '--lock "C:\file" --deep --deep', '--lock ""', '--lock "C:\file" --help')) {
        $result = Run-Cli $arguments
        Check ($result.Code -eq 2 -and $result.Out -eq '' -and $result.Err.Length -gt 0) "invalid usage: $arguments"
    }
    $missing = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString() + ' missing 日本.txt')
    $result = Run-Cli "--lock `"$missing`" --json"
    $json = ConvertFrom-Json -InputObject $result.Out
    Check ($result.Code -eq 1 -and $result.Err -eq '' -and $json.path -ceq $missing -and $json.path -is [string]) 'missing path JSON'
    $nativeCodeIsInteger = $json.error.native_code -is [int] -or $json.error.native_code -is [long]
    Check ($json.status -eq 'error' -and $json.error.category -eq 'invalid_path' -and $nativeCodeIsInteger) 'structured error'
    $result = Run-Cli "--lock `"$missing`""
    Check ($result.Code -eq 1 -and $result.Out -eq '' -and $result.Err.Contains('invalid path')) 'human error'
    Write-Output 'CLI contract cases passed (help, invalid arguments, missing JSON, missing human).'
    exit 0
}
$file = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString() + ' lock 日本.txt')
$handle = [System.IO.File]::Open($file, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::Read)
if ($Deep) {
    try {
        $result = Run-Cli "--lock `"$file`" --deep --json"
        $json = ConvertFrom-Json -InputObject $result.Out
        Check (($result.Code -eq 0 -or $result.Code -eq 3) -and $result.Err -eq '' -and $json.status -eq 'success' -and $json.path -ceq $file) 'deep JSON result'
        Check ($json.complete -is [bool] -and $json.native_scan.native_code -eq 0 -and -not $json.native_scan.limit_reached) 'deep completeness'
        Check (($result.Code -eq 0) -eq $json.complete) 'deep exit code matches completeness'
        $self = @($json.processes | Where-Object { $_.pid -eq $PID })
        Check ($self.Count -eq 1 -and $self[0].source -eq 'both' -and $self[0].resources -is [array] -and $self[0].resources -contains $file) 'deep merged resource'
        $result = Run-Cli "--lock `"$file`" --deep"
        Check (($result.Code -eq 0 -or $result.Code -eq 3) -and $result.Out.Contains([string]$PID) -and $result.Out.Contains($file)) 'deep human result'
        $handle.Dispose()
        $result = Run-Cli "--lock `"$file`" --deep --json"
        $json = ConvertFrom-Json -InputObject $result.Out
        Check (($result.Code -eq 0 -or $result.Code -eq 3) -and $json.processes -is [array] -and $json.processes.Count -eq 0) 'deep released'
        Write-Output 'Deep CLI held/released Unicode human/JSON cases passed.'
    } finally {
        $handle.Dispose()
        [System.IO.File]::Delete($file)
    }
    exit 0
}
try {
    $result = Run-Cli "--lock `"$file`" --json"
    $json = ConvertFrom-Json -InputObject $result.Out
    Check ($result.Code -eq 0 -and $result.Err -eq '' -and $json.path -ceq $file -and $json.status -eq 'success') 'held Unicode JSON'
    Check ($json.processes -is [array]) 'processes array'
    $self = @($json.processes | Where-Object { $_.pid -eq $PID })
    Check ($self.Count -eq 1 -and $self[0].pid -is [int] -and $self[0].name -is [string] -and $self[0].name.Length -gt 0) 'held process identity'
    $result = Run-Cli "--lock `"$file`""
    Check ($result.Code -eq 0 -and $result.Err -eq '' -and $result.Out.Contains($file) -and $result.Out.Contains([string]$PID) -and $result.Out.Contains($self[0].name)) 'human Unicode process'
    $handle.Dispose()
    $result = Run-Cli "--lock `"$file`" --json"
    $json = ConvertFrom-Json -InputObject $result.Out
    Check ($result.Code -eq 0 -and $json.processes -is [array] -and $json.processes.Count -eq 0) 'released JSON'
    $result = Run-Cli "--lock `"$file`""
    Check ($result.Code -eq 0 -and $result.Out.Contains('No locking processes found.')) 'released human'
    Write-Output 'CLI resource cases passed (held/released, human/JSON, Unicode).'
} finally {
    $handle.Dispose()
    [System.IO.File]::Delete($file)
}
