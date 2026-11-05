param([switch]$Test, [switch]$RebuildCodec)
$ErrorActionPreference = 'Stop'
$petRoot = $PSScriptRoot
$petBuild = Join-Path $petRoot 'build'
$petOut = Join-Path $petRoot 'out'
New-Item -ItemType Directory -Force -Path $petBuild,$petOut,(Join-Path $petBuild 'webp'),(Join-Path $petBuild 'app'),(Join-Path $petBuild 'tests') | Out-Null
$petVswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $petVswhere)) { throw 'Install Visual Studio Build Tools with Desktop development with C++ first.' }
$petVs = & $petVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$petVs) { throw 'No Visual C++ x64 toolchain found.' }
$petVcvars = Join-Path $petVs 'VC\Auxiliary\Build\vcvars64.bat'
$petCodec = Join-Path $petRoot 'third_party\libwebp-1.6.0'
if (!(Test-Path -LiteralPath (Join-Path $petCodec 'src\webp\decode.h'))) { throw 'Missing vendored libwebp source.' }
$petCommands = [System.Collections.Generic.List[string]]::new()
$petCommands.Add('@echo off')
$petCommands.Add(('call "{0}" >nul' -f $petVcvars))
$petCommands.Add('if errorlevel 1 exit /b 1')
$petCommands.Add(('cd /d "{0}"' -f $petRoot))
if ($RebuildCodec -or !(Test-Path -LiteralPath (Join-Path $petBuild 'webp.lib'))) {
    $petSources = @(@('dec','dsp','utils','enc') | ForEach-Object { Get-ChildItem -LiteralPath (Join-Path $petCodec "src\$_") -Filter '*.c' }) + @(Get-ChildItem -LiteralPath (Join-Path $petCodec 'sharpyuv') -Filter '*.c') | Sort-Object FullName
    $petCodecArgs = @('/nologo','/c','/O1','/GL','/Gy','/Gw','/MT','/DNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/DWEBP_USE_THREAD',('/I"{0}"' -f $petCodec),('/Fo"{0}\\"' -f (Join-Path $petBuild 'webp'))) + @($petSources | ForEach-Object { '"' + $_.FullName + '"' })
    [IO.File]::WriteAllLines((Join-Path $petBuild 'codec.rsp'),$petCodecArgs,[Text.UTF8Encoding]::new($false))
    $petObjects = $petSources | ForEach-Object { '"' + (Join-Path $petBuild ('webp\'+$_.BaseName+'.obj')) + '"' }
    [IO.File]::WriteAllLines((Join-Path $petBuild 'codec-lib.rsp'),(@('/nologo',('/OUT:"{0}"' -f (Join-Path $petBuild 'webp.lib'))) + $petObjects),[Text.UTF8Encoding]::new($false))
    $petCommands.Add('cl.exe @build\codec.rsp')
    $petCommands.Add('if errorlevel 1 exit /b 1')
    $petCommands.Add('lib.exe @build\codec-lib.rsp')
    $petCommands.Add('if errorlevel 1 exit /b 1')
}
$petCommon = @('/nologo','/std:c++17','/EHsc','/O1','/GL','/Gy','/Gw','/MT','/utf-8','/W4','/DUNICODE','/D_UNICODE','/DNDEBUG','/D_CRT_SECURE_NO_WARNINGS','/D_WIN32_WINNT=0x0A00',('/I"{0}"' -f (Join-Path $petCodec 'src')))
$petLinks = @('/link','/LTCG','/OPT:REF','/OPT:ICF','/INCREMENTAL:NO','build\webp.lib','user32.lib','gdi32.lib','gdiplus.lib','windowscodecs.lib','ole32.lib','shell32.lib','comdlg32.lib','uuid.lib')
$petAppArgs = $petCommon + @('src\assets.cpp','src\render.cpp','src\main.cpp','/Fo"build\app\\"','/Fe"out\PocketPet.exe"') + $petLinks + @('/SUBSYSTEM:WINDOWS','/MANIFEST:EMBED','/MANIFESTINPUT:src\app.manifest')
[IO.File]::WriteAllText((Join-Path $petBuild 'app.rsp'),($petAppArgs -join ' '),[Text.UTF8Encoding]::new($false))
$petCommands.Add('cl.exe @build\app.rsp')
$petCommands.Add('if errorlevel 1 exit /b 1')
if ($Test) {
    $petTestArgs = $petCommon + @('src\assets.cpp','src\render.cpp','tools\sample_art.cpp','tests\tests.cpp','/Fo"build\tests\\"','/Fe"build\PocketPetTests.exe"') + $petLinks + @('/SUBSYSTEM:CONSOLE')
    [IO.File]::WriteAllText((Join-Path $petBuild 'tests.rsp'),($petTestArgs -join ' '),[Text.UTF8Encoding]::new($false))
    $petCommands.Add('cl.exe @build\tests.rsp')
    $petCommands.Add('if errorlevel 1 exit /b 1')
    $petCommands.Add('build\PocketPetTests.exe')
    $petCommands.Add('if errorlevel 1 exit /b 1')
}
[IO.File]::WriteAllLines((Join-Path $petBuild 'compile.cmd'),$petCommands,[Text.Encoding]::Default)
& $env:ComSpec /d /c (Join-Path $petBuild 'compile.cmd')
if ($LASTEXITCODE -ne 0) { throw "Build failed (exit $LASTEXITCODE)." }
Copy-Item -LiteralPath (Join-Path $petRoot 'README.md') -Destination (Join-Path $petOut '使用说明.md')
Copy-Item -LiteralPath (Join-Path $petRoot 'THIRD_PARTY_NOTICES.md') -Destination $petOut
foreach($petExtra in @('community-pets.json','install-community-pets.ps1','PETS_SOURCES.md','ADDING_PETS.md','pet-template.json')) { Copy-Item -LiteralPath (Join-Path $petRoot $petExtra) -Destination $petOut }
New-Item -ItemType Directory -Force -Path (Join-Path $petOut 'pets') | Out-Null
Get-ChildItem -LiteralPath (Join-Path $petRoot 'pets') -Directory | ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $petOut 'pets') -Recurse -Force }
$petLicenses = Join-Path $petOut 'licenses'
New-Item -ItemType Directory -Force -Path $petLicenses | Out-Null
Copy-Item -LiteralPath (Join-Path $petCodec 'COPYING') -Destination (Join-Path $petLicenses 'libwebp-COPYING.txt')
Copy-Item -LiteralPath (Join-Path $petCodec 'PATENTS') -Destination (Join-Path $petLicenses 'libwebp-PATENTS.txt')
Copy-Item -LiteralPath (Join-Path $petRoot 'third_party\picojson.h') -Destination $petLicenses
Get-Item -LiteralPath (Join-Path $petOut 'PocketPet.exe') | Select-Object FullName,@{Name='SizeKiB';Expression={[math]::Round($_.Length/1KB,1)}}
