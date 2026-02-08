# Flash script for XIAO DMIC Audio Sample - creates DFU package and flashes via serial

Write-Host "Flashing DMIC Audio Sample firmware via DFU..." -ForegroundColor Cyan
Write-Host "Make sure the XIAO board is in bootloader mode (double-tap reset)" -ForegroundColor Yellow
Write-Host ""

# Check if hex file exists
$hexFile = "build\zephyr\zephyr.hex"
if (-Not (Test-Path $hexFile)) {
    Write-Host "Error: $hexFile not found. Run west build first." -ForegroundColor Red
    exit 1
}

Write-Host "Found hex file: $hexFile" -ForegroundColor Green

# Use full path to adafruit-nrfutil
$nrfutil = "C:\Python313\Scripts\adafruit-nrfutil.exe"

if (-Not (Test-Path $nrfutil)) {
    Write-Host "Error: adafruit-nrfutil not found at $nrfutil" -ForegroundColor Red
    Write-Host "Install with: pip install adafruit-nrfutil" -ForegroundColor Yellow
    exit 1
}

# Temporarily clear Python path to avoid DLL conflicts
$oldPath = $env:PATH
$env:PATH = "C:\Python313;C:\Python313\Scripts;C:\Windows\System32"

try {
    # Create DFU package
    Write-Host "`nCreating DFU package..." -ForegroundColor Cyan
    $packageFile = "build\zephyr\led_strip.zip"

    & $nrfutil dfu genpkg --dev-type 0x0052 --application $hexFile $packageFile

    if ($LASTEXITCODE -ne 0) {
        Write-Host "`nFailed to create DFU package" -ForegroundColor Red
        exit $LASTEXITCODE
    }

    Write-Host "Package created successfully: $packageFile" -ForegroundColor Green

    # Find COM port
    Write-Host "`nSearching for XIAO bootloader COM port..." -ForegroundColor Cyan
    $comPort = Get-WmiObject Win32_SerialPort | Where-Object { $_.Description -match "USB Serial Device" } | Select-Object -First 1

    if ($comPort) {
        $portName = $comPort.DeviceID
        Write-Host "Found bootloader at $portName" -ForegroundColor Green

        # Flash the package
        Write-Host "`nFlashing firmware..." -ForegroundColor Cyan
        & $nrfutil dfu serial --package $packageFile -p $portName -b 115200

        if ($LASTEXITCODE -eq 0) {
            Write-Host "`nFlashing completed successfully!" -ForegroundColor Green
            Write-Host "The board should reset automatically." -ForegroundColor Green
        } else {
            Write-Host "`nFlashing failed!" -ForegroundColor Red
            exit $LASTEXITCODE
        }
    } else {
        Write-Host "No bootloader COM port found!" -ForegroundColor Red
        Write-Host "Make sure the board is in bootloader mode (double-tap reset button)" -ForegroundColor Yellow
        exit 1
    }
} finally {
    # Restore PATH
    $env:PATH = $oldPath
}
