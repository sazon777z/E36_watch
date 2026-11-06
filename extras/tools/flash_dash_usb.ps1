# =============================================================================
# Прошивка дэшборда E36_watch по USB без кнопок, когда мост TunerStudio занял USB.
#
# После загрузки прошивка ~2–3 с работает как USB-CDC (COM-порт), затем мост
# переключает USB в режим хоста и порт пропадает. Скрипт ждет появления порта,
# сразу переводит ESP32-S3 в загрузчик ROM сбросом через USB-Serial-JTAG
# (как esptool) и записывает загрузчик, таблицу разделов, boot_app0 и прошивку.
# Область NVS (пробег, настройки, сопряжения Bluetooth) не трогается.
#
# Запуск (после сборки arduino-cli compile --build-path <папка>):
#   powershell -ExecutionPolicy Bypass -File extras\tools\flash_dash_usb.ps1 -BuildDir <папка сборки>
# Затем переподключите USB-C дэшборда или нажмите RESET.
# =============================================================================
param(
    [Parameter(Mandatory = $true)][string]$BuildDir,
    [string]$Port = 'COM4',
    [int]$WaitSec = 300
)

$pkg = "$env:LOCALAPPDATA\Arduino15\packages\esp32"
$esptool = Get-ChildItem "$pkg\tools\esptool_py" -Recurse -Filter esptool.exe -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$bootApp0 = Get-ChildItem "$pkg\hardware\esp32" -Recurse -Filter boot_app0.bin -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
$boot = Join-Path $BuildDir 'E36_watch.ino.bootloader.bin'
$part = Join-Path $BuildDir 'E36_watch.ino.partitions.bin'
$app  = Join-Path $BuildDir 'E36_watch.ino.bin'
foreach ($f in @($esptool, $bootApp0, $boot, $part, $app)) {
    if (-not $f -or -not (Test-Path $f)) { "Не найден файл: $f"; exit 2 }
}

"Жду порт $Port до $WaitSec с. Переподключите USB-C дэшборда или нажмите RESET."
$t0 = Get-Date
$caught = $false
while (((Get-Date) - $t0).TotalSeconds -lt $WaitSec) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) {
        try {
            $p = New-Object System.IO.Ports.SerialPort $Port, 115200
            $p.Open()
            # Последовательность esptool USBJTAGSerialReset: IO0 = 0 и сброс чипа
            $p.RtsEnable = $false; $p.DtrEnable = $false; Start-Sleep -Milliseconds 100
            $p.DtrEnable = $true;  $p.RtsEnable = $false; Start-Sleep -Milliseconds 100
            $p.RtsEnable = $true;  $p.DtrEnable = $false; $p.RtsEnable = $true; Start-Sleep -Milliseconds 100
            $p.DtrEnable = $false; $p.RtsEnable = $false
            $p.Close()
            "{0:HH:mm:ss.fff} порт пойман, чип переведен в загрузчик" -f (Get-Date)
            $caught = $true
            break
        } catch {
            Start-Sleep -Milliseconds 30
        }
    } else {
        Start-Sleep -Milliseconds 30
    }
}
if (-not $caught) { "Порт $Port не появился за $WaitSec с"; exit 3 }

# Ждем, пока чип вернется в системе как загрузчик ROM
$t1 = Get-Date
Start-Sleep -Milliseconds 700
while (((Get-Date) - $t1).TotalSeconds -lt 10 -and -not ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port)) {
    Start-Sleep -Milliseconds 100
}
Start-Sleep -Milliseconds 500

& $esptool --chip esp32s3 --port $Port --baud 921600 --before default-reset --after hard-reset write-flash -z `
    --flash-mode keep --flash-freq keep --flash-size keep `
    0x0 $boot 0x8000 $part 0xe000 $bootApp0 0x10000 $app |
    Select-String -NotMatch '^Writing at|^\s*$'
"esptool exit=$LASTEXITCODE"
