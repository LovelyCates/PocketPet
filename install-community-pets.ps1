param([string]$Destination, [string[]]$Ids, [string]$Cache)
$ErrorActionPreference='Stop'
if(!$Destination) { $Destination=if(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'out\PocketPet.exe')) { Join-Path $PSScriptRoot 'out\pets' } else { Join-Path $PSScriptRoot 'pets' } }
$petRoot=[IO.Path]::GetFullPath($Destination).TrimEnd('\')
New-Item -ItemType Directory -Force -Path $petRoot | Out-Null
if(!$Cache) { $Cache=Join-Path $petRoot '.download-cache' }
New-Item -ItemType Directory -Force -Path $Cache | Out-Null
$petCatalog=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'community-pets.json') -Raw | ConvertFrom-Json
if($Ids) { $petEntries=@($petCatalog.pets | Where-Object id -In $Ids); foreach($petId in $Ids) { if($petId -notin $petEntries.id) { throw "Unknown pet: $petId" } } } else { $petEntries=$petCatalog.pets }
foreach($petEntry in $petEntries) {
    if($petEntry.id -notmatch '^[a-z0-9][a-z0-9-]+$') { throw 'Invalid pet directory name' }
    $petTarget=Join-Path $petRoot $petEntry.id
    if(Test-Path -LiteralPath $petTarget) { Write-Output "Already present, left unchanged: $($petEntry.name)"; continue }
    $petStage=Join-Path $petRoot ('.download-'+[guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $petStage | Out-Null
    foreach($petFile in $petEntry.files) {
        if($petFile.name -notin @('pet.json','spritesheet.webp','SOURCE-LICENSE.md')) { throw 'Unexpected asset filename' }
        if($petFile.sha256 -notmatch '^[a-f0-9]{64}$' -or $petFile.url -notmatch '^https://raw\.githubusercontent\.com/') { throw 'Unpinned or unsupported asset source' }
        $petCached=Join-Path $Cache $petFile.sha256
        if(!(Test-Path -LiteralPath $petCached)) { Invoke-WebRequest -Uri $petFile.url -OutFile $petCached -TimeoutSec 60 }
        if((Get-FileHash -LiteralPath $petCached -Algorithm SHA256).Hash.ToLowerInvariant() -ne $petFile.sha256) { throw "Asset checksum mismatch: $($petEntry.id)/$($petFile.name)" }
        Copy-Item -LiteralPath $petCached -Destination (Join-Path $petStage $petFile.name)
    }
    Copy-Item -LiteralPath (Join-Path $petStage 'pet.json') -Destination (Join-Path $petStage 'source-pet.json')
    $petManifest=Get-Content -LiteralPath (Join-Path $petStage 'pet.json') -Raw | ConvertFrom-Json
    $petManifest | Add-Member -NotePropertyName id -NotePropertyValue $petEntry.id -Force
    $petManifest | Add-Member -NotePropertyName displayName -NotePropertyValue $petEntry.name -Force
    $petManifest | Add-Member -NotePropertyName collection -NotePropertyValue $petEntry.collection -Force
    $petManifest | Add-Member -NotePropertyName spritesheetPath -NotePropertyValue 'spritesheet.webp' -Force
    $petManifest | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $petStage 'pet.json') -Encoding utf8
    $petSource=@("# $($petEntry.name)","", "Collection: $($petEntry.collection)","Author: $($petEntry.author)","Source: $($petEntry.source)","Pinned commit: $($petEntry.commit)","",'Downloaded for this local personal companion installation. Source images are unchanged. The local manifest only adds the Chinese label, ID and collection. See SOURCE-LICENSE.md for the upstream terms. Do not present these fan assets as PocketPet originals.')
    $petSource | Set-Content -LiteralPath (Join-Path $petStage 'SOURCE.md') -Encoding utf8
    # Both directories are checked before the native PowerShell move.
    foreach($petCheckPath in @($petStage,$petTarget)) { $petFull=[IO.Path]::GetFullPath($petCheckPath); if(!$petFull.StartsWith($petRoot+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Asset destination is outside pets' } }
    Move-Item -LiteralPath $petStage -Destination $petTarget
    Write-Output "Installed: $($petEntry.collection) / $($petEntry.name)"
}
Write-Output 'Refresh the pet list in PocketPet to switch characters.'
