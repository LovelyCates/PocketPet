param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Test }
$petArchive = Join-Path $PSScriptRoot 'PocketPet-v0.4.1-win-x64.zip'
# Stage reviewed repository assets, never unrelated locally imported pets.
$petStage = Join-Path $PSScriptRoot ('build\package-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $petStage | Out-Null
foreach ($petFile in @('PocketPet.exe','使用说明.md','THIRD_PARTY_NOTICES.md','licenses','community-pets.json','install-community-pets.ps1','PETS_SOURCES.md','ADDING_PETS.md','pet-template.json')) { Copy-Item -LiteralPath (Join-Path $PSScriptRoot "out\$petFile") -Destination $petStage -Recurse }
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'pets') -Destination $petStage -Recurse
Compress-Archive -Path (Join-Path $petStage '*') -DestinationPath $petArchive -Force
Get-Item -LiteralPath $petArchive | Select-Object FullName,@{Name='SizeKiB';Expression={[math]::Round($_.Length/1KB,1)}}
