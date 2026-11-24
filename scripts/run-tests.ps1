# Compile and run the headless core test suite (tests/environ_tests.cpp + src/core/*.cpp).
# Enforces /W4 /WX on all project C++ sources so zero warnings are allowed.
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$arch = & "$PSScriptRoot\native-platform.ps1"
$outDir = Join-Path $repoRoot "bin\$arch\Tests"
$objDir = Join-Path $repoRoot "temp\$arch\Tests"

New-Item -ItemType Directory -Force -Path $outDir | Out-Null
New-Item -ItemType Directory -Force -Path $objDir | Out-Null

$includes = @(
    "/I$repoRoot",
    "/I$repoRoot\src\core",
    "/I$repoRoot\extern\spdlog\include",
    "/I$repoRoot\extern\pnq\include",
    "/I$repoRoot\extern\sqlite3\src\sqlite3",
    "/I$repoRoot\extern\tomlplusplus\include",
    "/I$repoRoot\extern\fkYAML\include"
)

# Compile sqlite3.c separately without /W4 /WX (third-party C code)
$sqliteObj = Join-Path $objDir "sqlite3.obj"
if (-not (Test-Path $sqliteObj)) {
    Write-Host "[tests] Compiling sqlite3.c..."
    & cl /nologo /c /MDd /Zi /W0 /Fo"$sqliteObj" "$repoRoot\extern\sqlite3\src\sqlite3\sqlite3.c"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

$cppSources = @(
    "$repoRoot\precomp.cpp",
    "$repoRoot\src\core\AppSettings.cpp",
    "$repoRoot\src\core\Base16Theme.cpp",
    "$repoRoot\src\core\EnvExport.cpp",
    "$repoRoot\src\core\EnvGridModel.cpp",
    "$repoRoot\src\core\EnvStore.cpp",
    "$repoRoot\src\core\EnvWriter.cpp",
    "$repoRoot\src\core\KnowledgeBase.cpp",
    "$repoRoot\src\core\SnapshotStore.cpp",
    "$repoRoot\tests\environ_tests.cpp"
)

$exePath = Join-Path $outDir "environ_tests.exe"
Write-Host "[tests] Compiling & linking environ_tests.exe (/W4 /WX)..."

$clArgs = @(
    "/nologo",
    "/std:c++latest",
    "/EHsc",
    "/MDd",
    "/Zi",
    "/W4",
    "/WX",
    "/permissive-",
    "/DSPDLOG_USE_STD_FORMAT",
    "/D_DEBUG",
    "/DUNICODE",
    "/D_UNICODE",
    "/Fo$objDir\",
    "/Fd$objDir\environ_tests.pdb",
    "/Fe$exePath"
) + $includes + $cppSources + @(
    $sqliteObj,
    "/link",
    "advapi32.lib",
    "shell32.lib",
    "ole32.lib",
    "user32.lib"
)

& cl @clArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[tests] Running environ_tests.exe..."
Push-Location $repoRoot
try {
    & $exePath
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
