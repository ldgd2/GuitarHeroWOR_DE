# GuitarHeroWOR_DE - preparar un PC desde cero para seguir el desarrollo.
# Se lanza con setup.bat (en la raiz del repo). Se puede repetir sin miedo: lo que ya esta, se salta.
#
#   setup.bat                          instala todo y compila (si ya tienes los archivos del juego en re-wor\assets)
#   setup.bat -Iso "D:\juegos\wor.iso" ademas extrae TU ISO de Xbox 360 a re-wor\assets
#   setup.bat -ConMovil                ademas instala Flutter + Android Studio y compila la app de la camara del movil
#   setup.bat -SinCompilar             solo instala dependencias, sin compilar el juego
#   setup.bat -GuardarParchesSDK       copia tus cambios en re-wor\thirdparty\rexglue-sdk a re-wor\patches (para subirlos)
#
# Que hace, en orden:
#   1. Herramientas (con winget): Git, Python 3.14, Node.js LTS, CMake, LLVM/Clang, Visual Studio 2022 Build Tools (C++ y Windows SDK)
#   2. Repositorios de terceros: rexglue-sdk (version fijada + nuestros cambios) y Guitar Hero SDK (diccionario de nombres)
#   3. Dependencias de Python (requirements.txt) y del editor web (npm), y compila EditorSong
#   4. Archivos del juego: los extrae de tu ISO si se la das (el repo NO incluye nada del juego)
#   5. Compila re-wor: configurar CMake, generar el codigo del xex (solo la primera vez) y compilar
param(
    [string]$Iso = "",
    [switch]$ConMovil,
    [switch]$SinCompilar,
    [switch]$SinInstalar,
    [switch]$GuardarParchesSDK
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
Set-Location $Root

# ---- versiones fijadas ------------------------------------------------------------------------------------------
$RexUrl     = "https://github.com/rexglue/rexglue-sdk.git"
$RexTag     = "v0.10.1"
$RexCommit  = "bd833a2ab69d305f99ddf57418fc38ffec8af93d"
$GhsdkUrl   = "https://gitgud.io/fretworks/guitar-hero-sdk.git"
$GhsdkCommit= "df55ce7c26cbb72793af9c6d715c7c5a1a07e474"
$XisoZip    = "https://github.com/XboxDev/extract-xiso/releases/download/build-202609111233/extract-xiso-Win64_Release.zip"
$Preset     = "win-amd64-relwithdebinfo"

$RexDir   = Join-Path $Root "re-wor\thirdparty\rexglue-sdk"
$RexPatch = Join-Path $Root "re-wor\patches\rexglue-sdk"
$GhsdkDir = Join-Path $Root "tools\ghsdk"
$Assets   = Join-Path $Root "re-wor\assets"
$Bin      = Join-Path $Root "tools\_bin"

$script:Avisos = New-Object System.Collections.Generic.List[string]
function Paso($t)  { Write-Host ""; Write-Host "=== $t ===" -ForegroundColor Cyan }
function Ok($t)    { Write-Host "  [ok] $t" -ForegroundColor Green }
function Info($t)  { Write-Host "  $t" }
function Aviso($t) { Write-Host "  [!] $t" -ForegroundColor Yellow; $script:Avisos.Add($t) }
function Falla($t) { Write-Host ""; Write-Host "[ERROR] $t" -ForegroundColor Red; exit 1 }

function RecargarPath {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
}
function Existe($cmd) { return [bool](Get-Command $cmd -ErrorAction SilentlyContinue) }
function Version($cmd, $argsList) {
    try { $o = & $cmd @argsList 2>$null | Out-String } catch { return $null }
    $m = [regex]::Match($o, "(\d+)\.(\d+)(\.(\d+))?")
    if ($m.Success) { return [version]("{0}.{1}.{2}" -f $m.Groups[1].Value, $m.Groups[2].Value, ($(if ($m.Groups[4].Value) { $m.Groups[4].Value } else { "0" }))) }
    return $null
}
function Winget($id, $extra) {
    if ($SinInstalar) { Aviso "Falta $id (no se instala por -SinInstalar)"; return }
    if (-not (Existe "winget")) { Falla "No esta winget (Instalador de aplicaciones). Instalalo desde Microsoft Store y vuelve a ejecutar setup.bat." }
    Info "Instalando $id ..."
    $a = @("install", "--id", $id, "-e", "--accept-package-agreements", "--accept-source-agreements", "--disable-interactivity")
    if ($extra) { $a += $extra } else { $a += "--silent" }
    & winget @a
    if ($LASTEXITCODE -ne 0) { Info "(winget termino con codigo $LASTEXITCODE; se comprueba igualmente)" }
    RecargarPath
}
function Run($what, [scriptblock]$sb) {
    & $sb
    if ($LASTEXITCODE -ne 0) { Falla "$what fallo (codigo $LASTEXITCODE)." }
}

# ---- solo guardar los cambios del SDK -----------------------------------------------------------------------------
if ($GuardarParchesSDK) {
    Paso "Guardando tus cambios de rexglue-sdk en re-wor\patches\rexglue-sdk"
    if (-not (Test-Path (Join-Path $RexDir ".git"))) { Falla "re-wor\thirdparty\rexglue-sdk no es un clon de git (setup.bat lo clona asi)." }
    $files = @(git -C $RexDir diff --name-only --ignore-submodules HEAD) + @(git -C $RexDir ls-files --others --exclude-standard)
    $files = $files | Where-Object { $_ -and -not $_.StartsWith("thirdparty/") -and -not $_.StartsWith("out/") }
    foreach ($f in $files) {
        $dst = Join-Path $RexPatch $f
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item (Join-Path $RexDir $f) $dst -Force
        Info $f
    }
    Ok "$(@($files).Count) archivos. Ahora haz commit de re-wor\patches."
    exit 0
}

Write-Host "GuitarHeroWOR_DE - preparando este PC" -ForegroundColor Cyan
Write-Host "Repositorio: $Root"

# ---- 1. herramientas ------------------------------------------------------------------------------------------------
Paso "1/5 Herramientas"
RecargarPath

if (-not (Existe "git")) { Winget "Git.Git" }
if (-not (Existe "git")) { Falla "No se pudo instalar Git." }
Ok "Git $(Version git @('--version'))"

# Python >= 3.11 (el "python" de Microsoft Store es un acceso directo vacio: se comprueba que funcione de verdad)
$pyOk = $false
try { $pyOk = ((& python -c "import sys;print(sys.version_info>=(3,11))" 2>$null) -eq "True") } catch {}
if (-not $pyOk) {
    Winget "Python.Python.3.14" @("--silent", "--override", "/quiet InstallAllUsers=0 PrependPath=1 Include_launcher=1")
    try { $pyOk = ((& python -c "import sys;print(sys.version_info>=(3,11))" 2>$null) -eq "True") } catch {}
}
if (-not $pyOk) { Falla "Python 3.11+ no responde. Si se acaba de instalar, cierra esta ventana y vuelve a ejecutar setup.bat. Si sigue fallando, desactiva 'python.exe' en Configuracion > Aplicaciones > Alias de ejecucion de aplicaciones." }
Ok "Python $(Version python @('--version'))"

$nv = $null; if (Existe "node") { $nv = Version node @("--version") }
if (-not $nv -or $nv -lt [version]"18.0.0") { Winget "OpenJS.NodeJS.LTS"; $nv = Version node @("--version") }
if (-not $nv) { Falla "No se pudo instalar Node.js." }
Ok "Node.js $nv"

$cv = $null; if (Existe "cmake") { $cv = Version cmake @("--version") }
if (-not $cv -or $cv -lt [version]"3.25.0") { Winget "Kitware.CMake"; $cv = Version cmake @("--version") }
if (-not $cv) { Falla "No se pudo instalar CMake." }
Ok "CMake $cv"

# Clang: CMakePresets.json lo busca en C:\Program Files\LLVM (donde lo deja el instalador oficial)
$clang = "C:\Program Files\LLVM\bin\clang.exe"
$lv = $null; if (Test-Path $clang) { $lv = Version $clang @("--version") }
if (-not $lv -or $lv -lt [version]"20.0.0") { Winget "LLVM.LLVM"; if (Test-Path $clang) { $lv = Version $clang @("--version") } }
if (-not $lv) { Falla "No se pudo instalar LLVM/Clang en C:\Program Files\LLVM." }
Ok "Clang $lv"

# Visual Studio 2022 con C++ (enlazador, rc.exe, ninja y el Windows SDK)
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function VsPath {
    if (-not (Test-Path $vswhere)) { return $null }
    $p = & $vswhere -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($p) { return $p.Trim() } else { return $null }
}
if (-not (VsPath)) {
    Info "Visual Studio Build Tools tarda (varios GB). Windows pedira permiso de administrador."
    Winget "Microsoft.VisualStudio.2022.BuildTools" @("--override", "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --add Microsoft.VisualStudio.Component.Windows11SDK.26100")
}
$vs = VsPath
if (-not $vs) { Falla "Falta Visual Studio 2022 con 'Desarrollo para el escritorio con C++'. Instalalo desde el Visual Studio Installer y vuelve a ejecutar setup.bat." }
Ok "Visual Studio: $vs"

# ---- 2. repositorios de terceros ------------------------------------------------------------------------------------
Paso "2/5 Repositorios de terceros"

if (-not (Test-Path (Join-Path $RexDir "CMakeLists.txt"))) {
    Info "Clonando rexglue-sdk $RexTag con sus submodulos (unos 400 MB)..."
    New-Item -ItemType Directory -Force (Split-Path $RexDir) | Out-Null
    Run "Clonar rexglue-sdk" { git -c core.longpaths=true clone --branch $RexTag --depth 1 --recurse-submodules --shallow-submodules -j 8 $RexUrl $RexDir }
    $head = (git -C $RexDir rev-parse HEAD).Trim()
    if ($head -ne $RexCommit) { Aviso "rexglue-sdk $RexTag apunta a $head y se esperaba $RexCommit (la etiqueta cambio en el original)." }
} else {
    Ok "rexglue-sdk ya esta"
}
# nuestros cambios al SDK: se copian los que sean mas nuevos que tu copia (no pisa lo que edites tu despues)
$n = 0
Get-ChildItem $RexPatch -Recurse -File | ForEach-Object {
    $rel = $_.FullName.Substring($RexPatch.Length + 1)
    $dst = Join-Path $RexDir $rel
    if (-not (Test-Path $dst) -or $_.LastWriteTimeUtc -gt (Get-Item $dst).LastWriteTimeUtc) {
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item $_.FullName $dst -Force; $n++
    }
}
Ok "rexglue-sdk $RexTag + cambios de re-wor ($n archivos actualizados)"

if (-not (Test-Path (Join-Path $GhsdkDir "SDKCode"))) {
    Info "Clonando Guitar Hero SDK (diccionario de nombres QBKey que usa EditorSong)..."
    git -c core.longpaths=true clone $GhsdkUrl $GhsdkDir
    if ($LASTEXITCODE -eq 0) { git -C $GhsdkDir -c advice.detachedHead=false checkout -q $GhsdkCommit }
    if ($LASTEXITCODE -ne 0) { Aviso "No se pudo clonar el Guitar Hero SDK: EditorSong funciona, pero mostrara algunos nombres como numeros." }
    else { Ok "Guitar Hero SDK" }
} else {
    Ok "Guitar Hero SDK ya esta"
}

# ---- 3. Python y editor --------------------------------------------------------------------------------------------
Paso "3/5 Dependencias de Python y EditorSong"
Run "pip install" { python -m pip install --disable-pip-version-check -q -r (Join-Path $Root "requirements.txt") }
Ok "Python: $((Get-Content (Join-Path $Root 'requirements.txt') | Where-Object { $_ -and -not $_.StartsWith('#') }) -join ', ')"

Push-Location (Join-Path $Root "EditorSong\web")
try {
    Run "npm ci" { npm ci --no-audit --no-fund --loglevel=error }
    Run "Compilar EditorSong" { npm run build --silent }
} finally { Pop-Location }
Ok "EditorSong compilado (EditorSong\web\dist)"

# ---- 4. archivos del juego -----------------------------------------------------------------------------------------
Paso "4/5 Archivos del juego (de tu copia)"
$xex = Join-Path $Assets "default.xex"
if (-not (Test-Path $xex) -and $Iso) {
    if (-not (Test-Path $Iso)) { Falla "No existe la ISO: $Iso" }
    $xiso = Get-ChildItem $Bin -Recurse -Filter "extract-xiso.exe" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $xiso) {
        Info "Descargando extract-xiso (XboxDev)..."
        New-Item -ItemType Directory -Force $Bin | Out-Null
        $zip = Join-Path $Bin "extract-xiso.zip"
        Invoke-WebRequest -UseBasicParsing $XisoZip -OutFile $zip
        Expand-Archive $zip (Join-Path $Bin "extract-xiso") -Force
        Remove-Item $zip
        $xiso = Get-ChildItem $Bin -Recurse -Filter "extract-xiso.exe" | Select-Object -First 1
    }
    Info "Extrayendo $Iso en re-wor\assets (unos minutos)..."
    Run "Extraer la ISO" { & $xiso.FullName -x $Iso -d $Assets }
}
if (Test-Path $xex) { Ok "re-wor\assets\default.xex" }
else { Aviso "Faltan los archivos del juego. Extrae tu ISO de Xbox 360 en re-wor\assets (o ejecuta: setup.bat -Iso ""ruta\a\tu.iso"") y vuelve a ejecutar setup.bat." }

# ---- 5. compilar re-wor ------------------------------------------------------------------------------------------------
Paso "5/5 Compilar re-wor"
if ($SinCompilar) {
    Info "Saltado (-SinCompilar)."
} elseif (-not (Test-Path $xex)) {
    Info "Saltado: sin default.xex no se puede generar el codigo del juego."
} elseif (Get-Process -Name "Guitar Hero Warriors of Rock" -ErrorAction SilentlyContinue) {
    Aviso "El juego esta abierto: no se compila (no se puede reemplazar el .exe). Cierralo y ejecuta build.bat."
} else {
    $codegen = if (Test-Path (Join-Path $Root "re-wor\generated\re_wor_init.h")) { "" } else { "&& echo Generando el codigo del juego (la primera vez tarda bastante)... && cmake --build out\build\$Preset --target re_wor_codegen" }
    $cmd = "call `"$Root\tools\setup\vsenv.bat`" && cd /d `"$Root\re-wor`" && cmake --preset $Preset $codegen && cmake --build out\build\$Preset"
    Run "La compilacion de re-wor" { cmd /c $cmd }
    Ok "re-wor\out\build\$Preset\Guitar Hero Warriors of Rock.exe"
}

# ---- opcional: app del movil ---------------------------------------------------------------------------------------------
if ($ConMovil) {
    Paso "Extra: app de la camara del movil (Flutter)"
    if (-not (Existe "flutter")) {
        $fl = "C:\dev\flutter"
        if (-not (Test-Path "$fl\bin\flutter.bat")) {
            Info "Clonando Flutter (estable) en $fl ..."
            New-Item -ItemType Directory -Force "C:\dev" | Out-Null
            Run "Clonar Flutter" { git clone --depth 1 -b stable https://github.com/flutter/flutter.git $fl }
        }
        $up = [Environment]::GetEnvironmentVariable("Path", "User")
        if ($up -notlike "*$fl\bin*") { [Environment]::SetEnvironmentVariable("Path", "$up;$fl\bin", "User") }
        RecargarPath
    }
    if (-not (Test-Path "$env:LOCALAPPDATA\Android\Sdk")) {
        Winget "Google.AndroidStudio"
        Aviso "Abre Android Studio una vez para que descargue el Android SDK; luego ejecuta 'flutter doctor --android-licenses' y vuelve a ejecutar setup.bat -ConMovil."
    } else {
        Push-Location (Join-Path $Root "EditorSong\vcam_app")
        try {
            Run "flutter pub get" { flutter pub get }
            Run "Compilar la app" { flutter build apk --release }
            Ok "EditorSong\vcam_app\build\app\outputs\flutter-apk\app-release.apk"
        } finally { Pop-Location }
    }
}

# ---- resumen -------------------------------------------------------------------------------------------------------
Write-Host ""
if ($script:Avisos.Count) {
    Write-Host "Terminado con avisos:" -ForegroundColor Yellow
    $script:Avisos | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
} else {
    Write-Host "Todo listo." -ForegroundColor Green
}
Write-Host ""
Write-Host "  Juego:       run_game.bat"
Write-Host "  Recompilar:  build.bat"
Write-Host "  EditorSong:  EditorSong\EditorSong.bat"
Write-Host "  Mods:        wormod.bat"
