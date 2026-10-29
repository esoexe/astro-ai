# A.S.T.R.O AI: terminal install for Windows (no setup wizard, no admin rights).
#
#   irm https://esoexe.github.io/astro-ai/install.ps1 | iex
#
# Installs the CLEAN edition of the `astro` command and its local engine into
# %LOCALAPPDATA%\Programs\AstroAI-CLI and adds that folder to your user PATH.
# Downloads come from the official GitHub release and are checked against SHA256SUMS.txt.
#
# Options (set before running):  $env:ASTRO_VERSION = "1.5.0"   $env:ASTRO_HOME = "D:\Tools\astro"
# Uninstall:  $env:ASTRO_UNINSTALL = "1"; irm https://esoexe.github.io/astro-ai/install.ps1 | iex

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'   # makes Invoke-WebRequest much faster
[Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

$Repo = 'esoexe/astro-ai'
$Fallback = '__ASTRO_VERSION__'
$Dest = if ($env:ASTRO_HOME) { $env:ASTRO_HOME } else { Join-Path $env:LOCALAPPDATA 'Programs\AstroAI-CLI' }

function Step($m) { Write-Host "● " -ForegroundColor DarkYellow -NoNewline; Write-Host $m }
function Ok($m) { Write-Host "  ✓ " -ForegroundColor Green -NoNewline; Write-Host $m }
function Fail($m) { Write-Host "✗ $m" -ForegroundColor Red; throw $m }

function Set-UserPath([string]$dir, [bool]$add) {
  $cur = [Environment]::GetEnvironmentVariable('Path', 'User'); if (-not $cur) { $cur = '' }
  $parts = $cur.Split(';') | Where-Object { $_ -and ($_.TrimEnd('\') -ine $dir.TrimEnd('\')) }
  if ($add) { $parts = @($parts) + $dir }
  [Environment]::SetEnvironmentVariable('Path', ($parts -join ';'), 'User')
}

if ($env:ASTRO_UNINSTALL -eq '1') {
  Get-Process astro-server -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Dest*" } | Stop-Process -Force -ErrorAction SilentlyContinue
  if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
  Set-UserPath $Dest $false
  Ok 'A.S.T.R.O AI terminal install removed. Your chats, settings and keys were kept.'
  return
}

if (-not [Environment]::Is64BitOperatingSystem) { Fail 'A.S.T.R.O AI needs 64-bit Windows 10 or 11.' }

$Version = $env:ASTRO_VERSION
if (-not $Version) {
  try {
    $r = Invoke-WebRequest -Uri "https://github.com/$Repo/releases/latest" -UseBasicParsing -MaximumRedirection 5
    $u = $r.BaseResponse.ResponseUri; if (-not $u) { $u = $r.BaseResponse.RequestMessage.RequestUri }
    if ("$u" -match '/tag/v([^/]+)$') { $Version = $Matches[1] }
  } catch { }
}
if (-not $Version) { $Version = $Fallback }
if ($Version -like '__*') { Fail 'Could not find the latest version. Set one first: $env:ASTRO_VERSION = "1.5.0"' }
$Version = $Version.TrimStart('v')

$File = "AstroAI-$Version-windows-x64-portable-CLEAN.zip"
$Base = "https://github.com/$Repo/releases/download/v$Version"
$Tmp = Join-Path ([IO.Path]::GetTempPath()) ("astro-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $Tmp | Out-Null

Write-Host ''
Write-Host '  A.S.T.R.O AI' -NoNewline; Write-Host " v$Version · terminal install · Windows x64" -ForegroundColor DarkGray
Write-Host ''
try {
  Step "Downloading $File"
  Invoke-WebRequest -Uri "$Base/$File" -OutFile (Join-Path $Tmp $File) -UseBasicParsing

  Step 'Checking the download'
  $haveSums = $true
  try { Invoke-WebRequest -Uri "$Base/SHA256SUMS.txt" -OutFile (Join-Path $Tmp 'SHA256SUMS.txt') -UseBasicParsing } catch { $haveSums = $false }
  if (-not $haveSums) { Write-Host '  (SHA256SUMS.txt not found; skipped)' -ForegroundColor DarkGray }
  else {
    $line = Get-Content (Join-Path $Tmp 'SHA256SUMS.txt') | Where-Object { $_ -match ([Regex]::Escape($File) + '$') } | Select-Object -First 1
    if ($line) {
      $want = ($line -split '\s+')[0].ToLower()
      $got = (Get-FileHash (Join-Path $Tmp $File) -Algorithm SHA256).Hash.ToLower()
      if ($want -ne $got) { Fail "Checksum mismatch for $File. Nothing was installed." }
      Ok 'SHA-256 matches the release'
    } else { Write-Host '  (no checksum for this file; skipped)' -ForegroundColor DarkGray }
  }

  Step "Installing into $Dest"
  Expand-Archive -Path (Join-Path $Tmp $File) -DestinationPath $Tmp -Force
  $src = Get-ChildItem -Path $Tmp -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'astro.exe') } | Select-Object -First 1
  if (-not $src) { Fail "The archive doesn't look right (no astro.exe inside)." }
  Get-Process astro-server -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$Dest*" } | Stop-Process -Force -ErrorAction SilentlyContinue
  if (Test-Path $Dest) { Remove-Item -Recurse -Force $Dest }
  New-Item -ItemType Directory -Path $Dest -Force | Out-Null
  foreach ($f in 'astro.exe', 'astro-server.exe', 'edition.json', 'providers.json', 'README.txt') {
    $p = Join-Path $src.FullName $f; if (Test-Path $p) { Copy-Item $p $Dest }
  }
  Copy-Item -Recurse (Join-Path $src.FullName 'web') (Join-Path $Dest 'web')
  Get-ChildItem -Recurse $Dest | Unblock-File -ErrorAction SilentlyContinue

  Set-UserPath $Dest $true
  if (("$env:Path".Split(';') | ForEach-Object { $_.TrimEnd('\') }) -notcontains $Dest.TrimEnd('\')) { $env:Path = "$env:Path;$Dest" }
  Ok "astro.exe added to your PATH ($Dest)"
  try { $v = & (Join-Path $Dest 'astro.exe') --version; Ok $v } catch { }
} finally {
  Remove-Item -Recurse -Force $Tmp -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host '✓ Installed.' -ForegroundColor Green -NoNewline; Write-Host ' Open a project folder and run:'
Write-Host ''
Write-Host '    astro                      ' -ForegroundColor DarkYellow -NoNewline; Write-Host '# chat with the team in this folder' -ForegroundColor DarkGray
Write-Host '    astro -p "fix the tests"   ' -ForegroundColor DarkYellow -NoNewline; Write-Host '# one task, start to finish' -ForegroundColor DarkGray
Write-Host '    astro --help' -ForegroundColor DarkYellow
Write-Host ''
Write-Host 'Works in this window now; other terminals pick it up when you reopen them.' -ForegroundColor DarkGray
Write-Host 'The first run starts the local engine by itself and works right away with the free models that need no key.' -ForegroundColor DarkGray
Write-Host 'To add more free providers, open http://127.0.0.1:47612 in your browser while it runs (Settings -> Providers).' -ForegroundColor DarkGray
