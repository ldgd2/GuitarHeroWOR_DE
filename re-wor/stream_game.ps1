# Rutas absolutas fijas para evitar cadenas vacías
$ScriptRoot = "C:\Users\ldgd2\Documents\proyecto_lider\GuitarHeroWOR_DE\re-wor"
$GameExe    = "$ScriptRoot\out\build\win-amd64-relwithdebinfo\Guitar Hero Warriors of Rock.exe"
$AssetsDir  = "$ScriptRoot\assets"
$TraceLog   = "$ScriptRoot\re_wor_trace.log"

# Verificar que el ejecutable exista
if (-not (Test-Path $GameExe)) {
    Write-Host "[ERROR] No se encuentra el ejecutable en: $GameExe" -ForegroundColor Red
    exit 1
}

# Limpiar log anterior
if (Test-Path $TraceLog) {
    Remove-Item $TraceLog -Force -ErrorAction SilentlyContinue
}

Write-Host "========================================================"
Write-Host " Iniciando Guitar Hero: Warriors of Rock (re-wor)"
Write-Host " Ruta de assets: $AssetsDir"
Write-Host "========================================================"

$workDir = Split-Path $GameExe
$proc = Start-Process -FilePath $GameExe -ArgumentList "--game_data_root", "`"$AssetsDir`"" -WorkingDirectory $workDir -PassThru

# Esperar a que se cree el archivo de log
$timeout = 0
while (-not (Test-Path $TraceLog) -and -not $proc.HasExited -and $timeout -lt 100) {
    Start-Sleep -Milliseconds 50
    $timeout++
}

# Leer el log en tiempo real
if (Test-Path $TraceLog) {
    $stream = [System.IO.File]::Open($TraceLog, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
    $reader = New-Object System.IO.StreamReader($stream)
    
    while (-not $proc.HasExited) {
        $line = $reader.ReadLine()
        if ($null -ne $line) {
            [Console]::WriteLine($line)
        } else {
            Start-Sleep -Milliseconds 30
        }
    }
    
    while ($null -ne ($line = $reader.ReadLine())) {
        [Console]::WriteLine($line)
    }
    
    $reader.Close()
    $stream.Close()
}