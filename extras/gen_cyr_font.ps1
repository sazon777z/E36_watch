param(
    [string]$Out,
    [string]$FontName = "Segoe UI Semibold",
    [double]$PixelSize = 12.5,
    [string]$CName = "lv_font_cyr_12",
    [int]$LineHeight = 15,
    [int]$BaseLine = 3
)
Add-Type -AssemblyName System.Drawing

$font = New-Object System.Drawing.Font($FontName, [single]$PixelSize, [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
$fam = $font.FontFamily
$ascentPx = $fam.GetCellAscent($font.Style) * $PixelSize / $fam.GetEmHeight($font.Style)
$fmt = [System.Drawing.StringFormat]::GenericTypographic
$fmt.FormatFlags = $fmt.FormatFlags -bor [System.Drawing.StringFormatFlags]::MeasureTrailingSpaces

$start = 0x410; $count = 64
$bytes = New-Object System.Collections.Generic.List[byte]
$dsc = @()
$ox = 10; $oy = 4

for ($cp = $start; $cp -lt $start + $count; $cp++) {
    $ch = [string][char]$cp
    $bmp = New-Object System.Drawing.Bitmap 40, 40
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Black)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.DrawString($ch, $font, [System.Drawing.Brushes]::White, [single]$ox, [single]$oy, $fmt)
    $adv = $g.MeasureString($ch, $font, (New-Object System.Drawing.PointF 0, 0), $fmt).Width
    $g.Dispose()

    $minX = 99; $minY = 99; $maxX = -1; $maxY = -1
    for ($y = 0; $y -lt 40; $y++) { for ($x = 0; $x -lt 40; $x++) {
        if ($bmp.GetPixel($x, $y).R -ge 16) {
            if ($x -lt $minX) { $minX = $x }; if ($x -gt $maxX) { $maxX = $x }
            if ($y -lt $minY) { $minY = $y }; if ($y -gt $maxY) { $maxY = $y }
        }
    } }

    $baselineY = [int][Math]::Ceiling($oy + $ascentPx)
    $index = $bytes.Count
    if ($maxX -lt 0) {
        $dsc += "{.bitmap_index = $index, .adv_w = $([int][Math]::Round($adv*16)), .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}"
        continue
    }
    $w = $maxX - $minX + 1; $h = $maxY - $minY + 1
    $nib = New-Object System.Collections.Generic.List[int]
    for ($y = $minY; $y -le $maxY; $y++) { for ($x = $minX; $x -le $maxX; $x++) {
        $nib.Add(($bmp.GetPixel($x, $y).R) -shr 4)
    } }
    for ($i = 0; $i -lt $nib.Count; $i += 2) {
        $hi = $nib[$i]; $lo = if ($i + 1 -lt $nib.Count) { $nib[$i + 1] } else { 0 }
        $bytes.Add([byte](($hi -shl 4) -bor $lo))
    }
    $ofsX = $minX - $ox
    $ofsY = $baselineY - ($maxY + 1)
    $dsc += "{.bitmap_index = $index, .adv_w = $([int][Math]::Round($adv*16)), .box_w = $w, .box_h = $h, .ofs_x = $ofsX, .ofs_y = $ofsY}"
    $bmp.Dispose()
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine("/*")
[void]$sb.AppendLine(" * Cyrillic U+0410..U+044F, 12 px, 4 bpp. Generated from `"$FontName`" $PixelSize px (System.Drawing).")
[void]$sb.AppendLine(" * Latin, digits and symbols fall back to lv_font_montserrat_12.")
[void]$sb.AppendLine(" */")
[void]$sb.AppendLine('#include "lvgl.h"')
[void]$sb.AppendLine("")
[void]$sb.AppendLine("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
for ($i = 0; $i -lt $bytes.Count; $i += 16) {
    $line = ($bytes.GetRange($i, [Math]::Min(16, $bytes.Count - $i)) | ForEach-Object { "0x{0:x2}" -f $_ }) -join ", "
    [void]$sb.AppendLine("    $line,")
}
[void]$sb.AppendLine("};")
[void]$sb.AppendLine("")
[void]$sb.AppendLine("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
[void]$sb.AppendLine("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,")
foreach ($d in $dsc) { [void]$sb.AppendLine("    $d,") }
[void]$sb.AppendLine("};")
[void]$sb.AppendLine(@"

static const lv_font_fmt_txt_cmap_t cmaps[] = {
    {
        .range_start = $start, .range_length = $count, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

static lv_font_fmt_txt_glyph_cache_t cache;

static const lv_font_fmt_txt_dsc_t font_dsc = {
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
    .cache = &cache
};

const lv_font_t $CName = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = $LineHeight,
    .base_line = $BaseLine,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -1,
    .underline_thickness = 1,
    .dsc = &font_dsc,
    .fallback = &lv_font_montserrat_12,
    .user_data = NULL
};
"@)
[IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding $false))
"OK: $count glyphs, $($bytes.Count) bytes, ascent $([Math]::Round($ascentPx,2)) px -> $Out"
