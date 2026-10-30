param(
    [Parameter(Mandatory=$true)][string]$DataDir,
    [Parameter(Mandatory=$true)][string]$WorkDir
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$version = '9.0.2'
$url = 'https://github.com/GyanD/codexffmpeg/releases/download/9.0.2/ffmpeg-9.0.2-essentials_build.zip'
$expected = '60f467265b1e312373dbcd92200c2618a74850f98d3d078e94296bb3fa2047ba'
$destination = Join-Path ([IO.Path]::GetFullPath($DataDir)) 'tools/ffmpeg'
$executable = Join-Path $destination 'bin/ffmpeg.exe'
if (Test-Path -LiteralPath $destination) {
    if ((Test-Path -LiteralPath $executable) -and (Test-Path -LiteralPath (Join-Path $destination 'download.json'))) {
        $receipt = Get-Content -LiteralPath (Join-Path $destination 'download.json') -Raw | ConvertFrom-Json
        if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash -eq $receipt.executable_sha256) {
            Write-Output 'FFmpeg is already installed.'
            exit 0
        }
    }
    throw 'Existing FFmpeg folder is incomplete or changed. Rename it before retrying; it was not replaced.'
}
New-Item -ItemType Directory -Path $WorkDir -Force | Out-Null
$archive = Join-Path $WorkDir 'ffmpeg.zip'
$staging = Join-Path $WorkDir 'install'
New-Item -ItemType Directory -Path (Join-Path $staging 'bin') -Force | Out-Null
Write-Output "Downloading FFmpeg $version from Gyan's GitHub release..."
Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $archive -TimeoutSec 480
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $expected) {
    throw 'FFmpeg download checksum mismatch; nothing was installed.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead($archive)
try {
    # Extract only named files to our own fixed paths, never archive-supplied paths.
    foreach ($item in @(@('/bin/ffmpeg.exe','bin/ffmpeg.exe'),@('/LICENSE','LICENSE'),@('/README.txt','README.txt'))) {
        $entries = @($zip.Entries | Where-Object { $_.FullName.EndsWith($item[0], [StringComparison]::OrdinalIgnoreCase) })
        if ($entries.Count -ne 1) { throw "Unexpected FFmpeg archive layout: $($item[0])" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0],(Join-Path $staging $item[1]),$false)
    }
} finally { $zip.Dispose() }
@{version=$version;url=$url;archive_sha256=$expected;executable_sha256=(Get-FileHash -LiteralPath (Join-Path $staging 'bin/ffmpeg.exe') -Algorithm SHA256).Hash} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $staging 'download.json') -Encoding UTF8
New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
[IO.Directory]::Move([IO.Path]::GetFullPath($staging), $destination)
Write-Output "FFmpeg installed in $destination. MP4 export is ready."
