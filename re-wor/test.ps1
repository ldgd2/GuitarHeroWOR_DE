# Obtener la ruta absoluta de la carpeta raíz del proyecto donde vive este script
$ScriptRoot = $PSScriptRoot
if (-not $ScriptRoot) {
    $ScriptRoot = Get-Location
}

# Construir rutas absolutas limpias y seguras
$GameExe = Join-Path $ScriptRoot "out\build\win-amd64-relwithdebinfo\Guitar Hero Warriors of Rock.exe"
$AssetsDir = Join-Path $ScriptRoot "assets"
$TraceLog  = Join-Path $ScriptRoot "re_wor_trace.log"

Write-Host "[INFO] Buscando ejecutable en: $GameExe" -ForegroundColor Cyan

# Validar que el ejecutable realmente exista antes de lanzarlo
if (-not (Test-Path $GameExe)) {
    Write-Host "[ERROR] No se encontró el ejecutable. ¿Está seguro de que la compilación finalizó con éxito?" -ForegroundColor Red
    Write-Host "[ERROR] Verifique que exista el archivo en esa ruta." -ForegroundColor Red
    exit 1
}

# Limpiar log anterior si existe
if (Test-Path $TraceLog) {
    Remove-Item $TraceLog -Force -ErrorAction SilentlyContinue
}

# Iniciar el proceso con la ruta absoluta y directorio de trabajo explícito
$proc = Start-Process -FilePath $GameExe -ArgumentList "--game_data_root=`"$AssetsDir`"" -WorkingDirectory $ScriptRoot -PassThru

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
    
    # Volcar líneas restantes tras el cierre
    while ($null -ne ($line = $reader.ReadLine())) {
        [Console]::WriteLine($line)
    }
    
    $reader.Close()
    $stream.Close()
} else {
    Write-Host "[AVISO] El proceso terminó o no generó el archivo de traza (re_wor_trace.log)." -ForegroundColor Yellow
}