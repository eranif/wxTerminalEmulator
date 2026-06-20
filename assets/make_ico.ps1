$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$svg = Join-Path $scriptDir 'wxterminal-msw.svg'
$ico = Join-Path $scriptDir 'wxterminal.ico'
$tempDir = Join-Path $env:TEMP ('wxterminal_ico_' + [Guid]::NewGuid().ToString('N'))
$sizes = @(16, 32, 48, 64, 128, 256)

function Invoke-SvgRasterizer {
    param(
        [Parameter(Mandatory = $true)]
        [int]$Size,
        [Parameter(Mandatory = $true)]
        [string]$OutputPath
    )

    if (Get-Command rsvg-convert -ErrorAction SilentlyContinue) {
        & rsvg-convert -w $Size -h $Size $svg -o $OutputPath
        if ($LASTEXITCODE -ne 0) {
            throw "rsvg-convert failed for size $Size"
        }
        return
    }

    if (Get-Command inkscape -ErrorAction SilentlyContinue) {
        & inkscape --export-filename=$OutputPath -w $Size -h $Size $svg
        if ($LASTEXITCODE -ne 0) {
            throw "inkscape failed for size $Size"
        }
        return
    }

    throw 'Need rsvg-convert or inkscape on PATH.'
}

function Write-IcoFile {
    param(
        [Parameter(Mandatory = $true)]
        [array]$Entries,
        [Parameter(Mandatory = $true)]
        [string]$OutputPath
    )

    $offset = 6 + 16 * $Entries.Count
    $stream = New-Object System.IO.MemoryStream
    $writer = New-Object System.IO.BinaryWriter($stream)

    $writer.Write([byte[]](0, 0))
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$Entries.Count)

    foreach ($entry in $Entries) {
        $size = [int]$entry.Size
        $dirByte = 0
        if ($size -ne 256) {
            $dirByte = [byte]$size
        }

        $writer.Write([byte]$dirByte)
        $writer.Write([byte]$dirByte)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$entry.Bytes.Length)
        $writer.Write([UInt32]$offset)
        $offset += $entry.Bytes.Length
    }

    foreach ($entry in $Entries) {
        $writer.Write($entry.Bytes)
    }

    $writer.Flush()
    [System.IO.File]::WriteAllBytes($OutputPath, $stream.ToArray())
}

if (-not (Test-Path $svg)) {
    throw "Missing SVG source: $svg"
}

New-Item -ItemType Directory -Path $tempDir | Out-Null

try {
    $entries = foreach ($size in $sizes) {
        $png = Join-Path $tempDir ("icon_{0}.png" -f $size)
        Invoke-SvgRasterizer -Size $size -OutputPath $png
        [pscustomobject]@{
            Size  = $size
            Bytes = [System.IO.File]::ReadAllBytes($png)
        }
    }

    Write-IcoFile -Entries $entries -OutputPath $ico
    Get-Item $ico | Select-Object FullName,Length,LastWriteTime | Format-List
}
finally {
    if (Test-Path $tempDir) {
        Remove-Item -Recurse -Force $tempDir
    }
}
