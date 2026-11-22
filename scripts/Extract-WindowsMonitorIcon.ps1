param(
    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

if (-not ('ThermalrightShellIconExtractor' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ThermalrightShellIconExtractor
{
    [DllImport("shell32.dll", EntryPoint = "ExtractIconExW", CharSet = CharSet.Unicode)]
    public static extern uint ExtractIconEx(
        string fileName,
        int iconIndex,
        IntPtr[] largeIcons,
        IntPtr[] smallIcons,
        uint iconCount);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool DestroyIcon(IntPtr iconHandle);
}
'@
}

$fullOutput = [System.IO.Path]::GetFullPath($OutputPath)
$outputDirectory = [System.IO.Path]::GetDirectoryName($fullOutput)
if (-not (Test-Path -LiteralPath $outputDirectory)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

# Shell32 icon index 15 is the Windows "Monitor and Keyboard" system icon.
# Try the classic DLL first, then the modern Windows 10/11 MUN resource container.
$iconIndex = 15
$systemRoot = $env:SystemRoot
$candidates = @(
    (Join-Path $systemRoot 'System32\shell32.dll'),
    (Join-Path $systemRoot 'SystemResources\shell32.dll.mun')
)

foreach ($source in $candidates) {
    if (-not (Test-Path -LiteralPath $source)) { continue }

    $large = New-Object 'System.IntPtr[]' 1
    $small = New-Object 'System.IntPtr[]' 1
    $icon = [IntPtr]::Zero
    $smallIcon = [IntPtr]::Zero
    try {
        $count = [ThermalrightShellIconExtractor]::ExtractIconEx($source, $iconIndex, $large, $small, 1)
        $icon = $large[0]
        $smallIcon = $small[0]
        if ($count -gt 0 -and $icon -ne [IntPtr]::Zero) {
            $managedIcon = [System.Drawing.Icon]::FromHandle($icon)
            $outputStream = [System.IO.File]::Create($fullOutput)
            try { $managedIcon.Save($outputStream) }
            finally { $outputStream.Dispose() }
            if ((Test-Path -LiteralPath $fullOutput) -and (Get-Item -LiteralPath $fullOutput).Length -gt 100) {
                Write-Host "Extracted Windows Shell32 Monitor and Keyboard icon from: $source"
                Write-Host "Icon file: $fullOutput"
                exit 0
            }
        }
    }
    catch {
        # Try the next Windows system resource container.
    }
    finally {
        if ($icon -ne [IntPtr]::Zero) { [void][ThermalrightShellIconExtractor]::DestroyIcon($icon) }
        if ($smallIcon -ne [IntPtr]::Zero) { [void][ThermalrightShellIconExtractor]::DestroyIcon($smallIcon) }
    }
}

throw 'Could not extract the Windows Shell32 Monitor and Keyboard icon (index 15). The Windows system icon library may differ on this installation.'
