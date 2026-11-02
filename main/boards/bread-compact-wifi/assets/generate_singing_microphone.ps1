# 将已确认的透明素材缩放并转为LVGL使用的直通Alpha BGRA像素；不修改原造型。
Add-Type -AssemblyName System.Drawing
$sourcePath = Join-Path $PSScriptRoot 'singing_microphone.png'
$outputPath = Join-Path $PSScriptRoot 'singing_microphone.h'
$sourceImage = [System.Drawing.Image]::FromFile($sourcePath)
$spriteImage = New-Object System.Drawing.Bitmap 84,84
$spriteGraphics = [System.Drawing.Graphics]::FromImage($spriteImage)
try {
    $spriteGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $spriteGraphics.DrawImage($sourceImage,0,0,84,84)
    $header = New-Object System.Text.StringBuilder
    [void]$header.AppendLine('#pragma once')
    [void]$header.AppendLine('#include <lvgl.h>')
    [void]$header.AppendLine('// 由同目录脚本生成：84×84、BGRA8888、非预乘Alpha，像素常量保存在Flash。')
    [void]$header.AppendLine('alignas(4) inline constexpr uint8_t kSingingMicrophonePixels[] = {')
    for ($row = 0; $row -lt 84; $row++) {
        $values = for ($column = 0; $column -lt 84; $column++) {
            $pixel = $spriteImage.GetPixel($column,$row)
            '0x{0:X2},0x{1:X2},0x{2:X2},0x{3:X2}' -f $pixel.B,$pixel.G,$pixel.R,$pixel.A
        }
        [void]$header.AppendLine('    ' + ($values -join ',') + ',')
    }
    [void]$header.AppendLine('};')
    [void]$header.AppendLine('inline const lv_image_dsc_t kSingingMicrophoneImage = [] {')
    [void]$header.AppendLine('    lv_image_dsc_t image = {};')
    [void]$header.AppendLine('    image.header.magic = LV_IMAGE_HEADER_MAGIC;')
    [void]$header.AppendLine('    image.header.cf = LV_COLOR_FORMAT_ARGB8888;')
    [void]$header.AppendLine('    image.header.w = 84;')
    [void]$header.AppendLine('    image.header.h = 84;')
    [void]$header.AppendLine('    image.header.stride = 84 * 4;')
    [void]$header.AppendLine('    image.data_size = sizeof(kSingingMicrophonePixels);')
    [void]$header.AppendLine('    image.data = kSingingMicrophonePixels;')
    [void]$header.AppendLine('    return image;')
    [void]$header.AppendLine('}();')
    [System.IO.File]::WriteAllText($outputPath,$header.ToString(),[System.Text.UTF8Encoding]::new($false))
} finally {
    $spriteGraphics.Dispose()
    $spriteImage.Dispose()
    $sourceImage.Dispose()
}
