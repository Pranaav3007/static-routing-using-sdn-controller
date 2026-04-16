param(
    [Parameter(Mandatory = $true)]
    [string]$InputPath,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath,

    [string]$Title = ""
)

Add-Type -AssemblyName System.Drawing

$text = Get-Content -LiteralPath $InputPath -Raw
$lines = $text -split "`r?`n"
if ($lines.Count -eq 0) {
    $lines = @("")
}

$font = New-Object System.Drawing.Font("Consolas", 14)
$titleFont = New-Object System.Drawing.Font("Segoe UI Semibold", 16)
$probeBitmap = New-Object System.Drawing.Bitmap 1, 1
$probeGraphics = [System.Drawing.Graphics]::FromImage($probeBitmap)

$lineHeight = [int][Math]::Ceiling($font.GetHeight($probeGraphics)) + 4
$titleHeight = 0
$maxWidth = 0

if ($Title) {
    $titleSize = $probeGraphics.MeasureString($Title, $titleFont)
    $titleHeight = [int][Math]::Ceiling($titleSize.Height) + 12
    $maxWidth = [int][Math]::Ceiling($titleSize.Width)
}

foreach ($line in $lines) {
    $size = $probeGraphics.MeasureString($line, $font)
    $width = [int][Math]::Ceiling($size.Width)
    if ($width -gt $maxWidth) {
        $maxWidth = $width
    }
}

$probeGraphics.Dispose()
$probeBitmap.Dispose()

$padding = 20
$width = [Math]::Max(900, $maxWidth + ($padding * 2))
$height = [Math]::Max(200, ($lines.Count * $lineHeight) + $titleHeight + ($padding * 2))

$bitmap = New-Object System.Drawing.Bitmap $width, $height
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::ClearTypeGridFit
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$graphics.Clear([System.Drawing.Color]::FromArgb(20, 23, 28))

$headerBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(36, 41, 51))
$textBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(230, 236, 242))
$titleBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 214, 102))

if ($Title) {
    $graphics.FillRectangle($headerBrush, 0, 0, $width, $titleHeight + 10)
    $graphics.DrawString($Title, $titleFont, $titleBrush, $padding, 10)
}

$y = $padding + $titleHeight
foreach ($line in $lines) {
    $graphics.DrawString($line, $font, $textBrush, $padding, $y)
    $y += $lineHeight
}

$outDir = Split-Path -Parent $OutputPath
if ($outDir) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

$bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)

$headerBrush.Dispose()
$textBrush.Dispose()
$titleBrush.Dispose()
$font.Dispose()
$titleFont.Dispose()
$graphics.Dispose()
$bitmap.Dispose()
