# Uploads build/web/ to the home server and makes it the live site.
#
#   tools/deploy_web.ps1 [-Server scaramanga] [-Url https://kaizxo.christrewartha.com/]
#
# Each deploy goes into its own folder, ~/kaizxo/releases/<time>, and then
# the ~/kaizxo/site link is switched to it in one step, so nobody sees a
# half-uploaded site. The last few releases are kept. Also copies the files
# in deploy/, which set the server up the first time (see the README).
#
# Run tools/build_web.ps1 first. Needs SSH key login to the server.

param(
    [string]$Server = 'scaramanga',
    [string]$Url = 'https://kaizxo.christrewartha.com/',
    [int]$Keep = 5
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$site = Join-Path $root 'build/web'
if (-not (Test-Path (Join-Path $site 'index.html'))) { throw "build/web not found: run tools/build_web.ps1 first" }

function Invoke-Native {
    & $args[0] $args[1..($args.Count - 1)]
    if ($LASTEXITCODE) { throw "$($args[0]) failed (exit $LASTEXITCODE)" }
}

$release = Get-Date -Format 'yyyyMMdd-HHmmss'
$tar = Join-Path ([IO.Path]::GetTempPath()) "kaizxo-$release.tar.gz"
# Windows' own tar: a GNU tar earlier on the PATH (Git's) reads C: as a host name
Invoke-Native (Join-Path $env:SystemRoot 'System32/tar.exe') -czf $tar -C $site .
try {
    Invoke-Native ssh -o BatchMode=yes $Server 'mkdir -p ~/kaizxo/releases'
    Invoke-Native scp -q -o BatchMode=yes $tar (Join-Path $root 'deploy/kaizxo.service') (Join-Path $root 'deploy/setup_server.sh') "${Server}:kaizxo/"
} finally {
    Remove-Item $tar
}

# Unpack, switch the link with a rename (atomic), then prune old releases.
# No double quotes in here: Windows PowerShell doesn't escape them when it
# passes the script to ssh.
$remote = @'
set -e
cd ~/kaizxo
mkdir releases/{0}
tar -xzf kaizxo-{0}.tar.gz -C releases/{0}
rm kaizxo-{0}.tar.gz
ln -sfn releases/{0} site.new
mv -T site.new site
ls -1d releases/* | head -n -{1} | xargs -r rm -rf
if curl -fsS -o /dev/null http://127.0.0.1:8790/; then echo 'The server is serving it'; else echo 'kaizxo.service is not running: on the server, run  sudo bash ~/kaizxo/setup_server.sh'; fi
'@ -f $release, $Keep
Invoke-Native ssh -o BatchMode=yes $Server ($remote -replace "`r", '')
Write-Host "Deployed release $release to ${Server}:kaizxo/site"

# Check the public site has this build: the page names the tape by its hash
$tapName = (Get-ChildItem $site -Filter 'kaizxo-*.tap').Name
try {
    $page = Invoke-WebRequest $Url -UseBasicParsing -Headers @{ 'Cache-Control' = 'no-cache' }
    if ($page.Content.Contains($tapName)) { Write-Host "Live at $Url" }
    else { Write-Warning "$Url answered, but not with this build yet" }
} catch {
    Write-Warning "$Url isn't reachable yet: $($_.Exception.Message)"
}
