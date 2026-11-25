param(
    [Parameter(Mandatory=$true)][string]$Toolchain,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Get-Sha256([string]$Path) {
    $hash=[Security.Cryptography.SHA256]::Create()
    $stream=[IO.File]::OpenRead($Path)
    try {return ([BitConverter]::ToString($hash.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}
    finally {$stream.Dispose();$hash.Dispose()}
}
$compiler=Join-Path $Toolchain 'g++.exe'
$resources=Join-Path $Toolchain 'windres.exe'
foreach($file in @($compiler,$resources)) {
    if(-not (Test-Path -LiteralPath $file -PathType Leaf)) {throw "Missing build tool: $file"}
}
$outputPath=[IO.Path]::GetFullPath($Output)
[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$archive=Join-Path $PSScriptRoot 'vendor\DDrawCompat-v0.7.2.zip'
if((Get-Sha256 $archive) -ne 'a932a4dcb2100552b6918fd4a3cc7de138bb97448bbca2a7051954993a397b39') {
    throw 'DDrawCompat archive SHA256 differs.'
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=[IO.Compression.ZipFile]::OpenRead($archive)
try {
    $entry=$zip.GetEntry('ddraw.dll')
    if($null -eq $entry -or $entry.Length -ne 3653632) {throw 'DDrawCompat member size differs'}
    $input=$entry.Open(); $memory=[IO.MemoryStream]::new()
    try {$input.CopyTo($memory);$payload=$memory.ToArray()} finally {$input.Dispose();$memory.Dispose()}
} finally {$zip.Dispose()}
$hash=[Security.Cryptography.SHA256]::Create()
try {$digest=([BitConverter]::ToString($hash.ComputeHash($payload))).Replace('-','').ToLowerInvariant()} finally {$hash.Dispose()}
if($digest -ne '94895cf6ede8fd0f74ee1447fdc08b7044a9c65a1a25a49a4df893a0370975a3') {throw 'DDrawCompat member SHA256 differs'}
$embedded=Join-Path $outputPath 'embedded-ddraw.dll'
[IO.File]::WriteAllBytes($embedded,$payload)
$rc=Join-Path $outputPath 'launcher-combined.rc'
$resourceText=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'launcher.rc'))
$resourceText=$resourceText.Replace('"launcher.manifest"',('"'+(Join-Path $PSScriptRoot 'launcher.manifest').Replace('\','/')+'"'))
$text=$resourceText + "`r`n101 RCDATA `"" + $embedded.Replace('\','/') + "`"`r`n"
[IO.File]::WriteAllText($rc,$text,[Text.UTF8Encoding]::new($false))
$oldPath=$env:PATH
$env:PATH=$Toolchain+[IO.Path]::PathSeparator+$oldPath
Push-Location -LiteralPath $PSScriptRoot
try {
    Push-Location -LiteralPath $outputPath
    try {
        & $resources 'launcher-combined.rc' '-O' 'coff' '-o' 'launcher-res.o'
        if($LASTEXITCODE -ne 0) {throw 'Resource compilation failed'}
    } finally {Pop-Location}
    $prefixMap='-ffile-prefix-map='+$PSScriptRoot+'=source'
    & $compiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' '-municode' `
        '-Wl,--no-insert-timestamp' $prefixMap 'main.cpp' 'runtime.cpp' 'installation.cpp' 'target.cpp' 'editor_patch.cpp' `
        (Join-Path $outputPath 'launcher-res.o') '-ladvapi32' '-lshell32' `
        '-o' (Join-Path $outputPath 'T3DGM-Compatibility.exe')
    if($LASTEXITCODE -ne 0) {throw 'Launcher compilation failed'}
} finally {
    Pop-Location
    $env:PATH=$oldPath
}
Write-Output 'Built T3DGM-Compatibility.exe'
