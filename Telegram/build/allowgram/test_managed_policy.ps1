[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$QtDirectory,
 [Parameter(Mandatory=$true)][string]$OpenSSLDirectory,
 [Parameter(Mandatory=$true)][string]$Repository,
 [Parameter(Mandatory=$true)][string]$Output,
 [switch]$Client
)
$ErrorActionPreference='Stop'
$Repository=(Resolve-Path -LiteralPath $Repository).Path
$QtDirectory=(Resolve-Path -LiteralPath $QtDirectory).Path
$OpenSSLDirectory=(Resolve-Path -LiteralPath $OpenSSLDirectory).Path
New-Item -ItemType Directory -Force -Path $Output | Out-Null
$Output=(Resolve-Path -LiteralPath $Output).Path
$staticQt=(Get-Content -LiteralPath "$QtDirectory/lib/Qt6Core.prl") -contains 'QMAKE_PRL_CONFIG = static'
$runtime=if($staticQt){'/MT'}else{'/MD'}
$definitions=@()
if($staticQt){$definitions+='/DQT_STATIC'}
$modules=@('Qt6Core')
if($Client){$modules+='Qt6Network'}
$libraries=@("$OpenSSLDirectory/out/libcrypto.lib", 'ws2_32.lib', 'crypt32.lib', 'advapi32.lib', 'user32.lib', 'bcrypt.lib')
foreach($module in $modules){
 $libraries+="$QtDirectory/lib/$module.lib"
 if($staticQt){
  $deps=Get-Content -LiteralPath "$QtDirectory/lib/$module.prl" | Where-Object {$_.StartsWith('QMAKE_PRL_LIBS_FOR_CMAKE = ')}
  if(!$deps){throw "Static Qt dependencies missing: $module"}
  foreach($dep in $deps.Substring('QMAKE_PRL_LIBS_FOR_CMAKE = '.Length).Split(';')){
   if($dep.StartsWith('-L')){$libraries+='/LIBPATH:'+$dep.Substring(2).Trim('"')}
   elseif($dep.StartsWith('-l')){$libraries+=$dep.Substring(2)+'.lib'}
   else{$libraries+=$dep.Replace('$$[QT_INSTALL_LIBS]',"$QtDirectory/lib")}
  }
 }
}
$includes=@("$QtDirectory/include","$QtDirectory/include/QtCore","$QtDirectory/include/QtNetwork","$OpenSSLDirectory/include","$Repository/Telegram/SourceFiles") | ForEach-Object {"/I$_"}
$name=if($Client){'allowlist_managed_client_test'}else{'allowlist_managed_policy_test'}
$sources=@("$Repository/Telegram/SourceFiles/main/allowlist_managed_policy.cpp","$Repository/Telegram/SourceFiles/test/$name.cpp")
if($Client){$sources+="$Repository/Telegram/SourceFiles/main/allowlist_managed_client.cpp"}
$objects=@()
Push-Location -LiteralPath $Output
try {
 foreach($source in $sources){
  cl.exe /nologo /c /std:c++20 /EHsc $runtime /O1 /Gy /Zc:__cplusplus /permissive- /utf-8 /W4 /DQT_NO_DEBUG @definitions @includes $source
  if($LASTEXITCODE -ne 0){throw "Compile failed: $source"}
  $objects+=([IO.Path]::GetFileNameWithoutExtension($source)+'.obj')
 }
 link.exe /nologo "/OUT:$name.exe" /OPT:REF @objects @libraries
 if($LASTEXITCODE -ne 0){throw 'Native managed test link failed'}
 $previous=$env:PATH
 try {
  $env:PATH="$QtDirectory/bin;$previous"
  & "./$name.exe" "$Repository/Telegram/SourceFiles/test/allowlist_managed_vectors.json" "$Output/receipt.json"
  $code=$LASTEXITCODE
  if($code -ne 0){exit $code}
 } finally {$env:PATH=$previous}
} finally {Pop-Location}
