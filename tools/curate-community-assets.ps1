$ErrorActionPreference='Stop'
$petProject=Split-Path -Parent $PSScriptRoot
$petSeed=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'community-pets.seed.json') -Raw | ConvertFrom-Json
$petCache=Join-Path $petProject 'build\community-cache'
New-Item -ItemType Directory -Force -Path $petCache | Out-Null
$petSeen=@{}
foreach($petEntry in $petSeed.pets) {
    foreach($petFile in $petEntry.files) {
        if($petSeen.ContainsKey($petFile.url)) { $petDigest=$petSeen[$petFile.url] }
        else {
            $petTemp=Join-Path $petCache ([guid]::NewGuid().ToString('N')+'.download')
            Invoke-WebRequest -Uri $petFile.url -OutFile $petTemp -TimeoutSec 60
            $petDigest=(Get-FileHash -LiteralPath $petTemp -Algorithm SHA256).Hash.ToLowerInvariant()
            $petFinal=Join-Path $petCache $petDigest
            if(!(Test-Path -LiteralPath $petFinal)) { Copy-Item -LiteralPath $petTemp -Destination $petFinal }
            $petSeen[$petFile.url]=$petDigest
        }
        $petFile | Add-Member -NotePropertyName sha256 -NotePropertyValue $petDigest -Force
    }
    Write-Output "Fetched: $($petEntry.collection) / $($petEntry.name)"
}
$petSeed | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $petProject 'community-pets.json') -Encoding utf8
Write-Output 'Pinned community-pets.json written'
