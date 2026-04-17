param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPrefix,

    [int]$LinesPerPage = 42
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

Add-Type -AssemblyName System.Drawing

$sourcePath = (Resolve-Path $InputPath).Path
$outputPrefixPath = $OutputPrefix
$outputDir = Split-Path -Parent $outputPrefixPath
if (-not [string]::IsNullOrWhiteSpace($outputDir)) {
    New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
}

$content = Get-Content -Path $sourcePath
if ($content.Count -eq 0) {
    $content = @("")
}

$font = New-Object System.Drawing.Font("Consolas", 16)
$titleFont = New-Object System.Drawing.Font("Consolas", 18, [System.Drawing.FontStyle]::Bold)
$brush = [System.Drawing.Brushes]::Black
$lineBrush = [System.Drawing.Brushes]::DimGray
$background = [System.Drawing.Color]::FromArgb(250, 250, 250)
$borderPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(220, 220, 220))

$sampleBitmap = New-Object System.Drawing.Bitmap(10, 10)
$graphics = [System.Drawing.Graphics]::FromImage($sampleBitmap)
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$lineHeight = [Math]::Ceiling($font.GetHeight($graphics) + 6)
$titleHeight = 52
$leftPadding = 32
$topPadding = 28
$lineNumberWidth = 72
$pageWidth = 1700
$pageHeight = $topPadding + $titleHeight + ($LinesPerPage * $lineHeight) + 24
$graphics.Dispose()
$sampleBitmap.Dispose()

$totalPages = [Math]::Ceiling($content.Count / [double]$LinesPerPage)
if ($totalPages -lt 1) {
    $totalPages = 1
}

for ($pageIndex = 0; $pageIndex -lt $totalPages; $pageIndex++) {
    $bitmap = New-Object System.Drawing.Bitmap($pageWidth, $pageHeight)
    $g = [System.Drawing.Graphics]::FromImage($bitmap)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
    $g.Clear($background)
    $g.DrawRectangle($borderPen, 0, 0, $pageWidth - 1, $pageHeight - 1)

    $title = "{0}  ({1}/{2})" -f [System.IO.Path]::GetFileName($sourcePath), ($pageIndex + 1), $totalPages
    $g.DrawString($title, $titleFont, $brush, $leftPadding, 14)

    $startLine = $pageIndex * $LinesPerPage
    $endLine = [Math]::Min($startLine + $LinesPerPage, $content.Count)

    for ($index = $startLine; $index -lt $endLine; $index++) {
        $relativeLine = $index - $startLine
        $y = $topPadding + $titleHeight + ($relativeLine * $lineHeight)
        $lineNumber = "{0,4}" -f ($index + 1)
        $lineText = $content[$index]
        $g.DrawString($lineNumber, $font, $lineBrush, $leftPadding, $y)
        $g.DrawString($lineText, $font, $brush, $leftPadding + $lineNumberWidth, $y)
    }

    $fileName = if ($totalPages -eq 1) {
        "{0}.png" -f $outputPrefixPath
    } else {
        "{0}-{1:D2}.png" -f $outputPrefixPath, ($pageIndex + 1)
    }

    $bitmap.Save($fileName, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose()
    $bitmap.Dispose()
}

$titleFont.Dispose()
$font.Dispose()
$borderPen.Dispose()
