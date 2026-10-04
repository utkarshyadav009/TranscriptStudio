# Downloads the two speech models into models\ and checks them. Safe to run again.
$ErrorActionPreference = "Stop"
$dir = Join-Path $PSScriptRoot "..\models"
$models = @(
    @{ repo = "nvidia/parakeet-tdt-0.6b-v3"; rev = "541d1f99c6b0c3cd0b11a95167540bb8edefd82b"
       file = "parakeet-tdt-0.6b-v3.q8_0.gguf"; sha = "e3880d0aaaaf2c308ea2c35016b2b895c423eb3fda924c1b463d1c19b7f4d32e" },
    @{ repo = "nvidia/Nemotron-3-Diarization"; rev = "f667ed73aee57d40cc39428eb768b4fd87a0a29e"
       file = "Nemotron-3-Diarization.q8_0.gguf"; sha = "08456d9e22cd9a323c0364d98375f3746d6e68507ebb705cd46438c534c7a3a1" }
)
foreach ($m in $models) {
    $out = Join-Path $dir $m.file
    if ((Test-Path $out) -and ((Get-FileHash $out -Algorithm SHA256).Hash.ToLower() -eq $m.sha)) {
        Write-Host "ok        $($m.file)"; continue
    }
    Write-Host "download  $($m.file)"
    curl.exe -fL --retry 3 -o $out "https://huggingface.co/$($m.repo)/resolve/$($m.rev)/$($m.file)"
    if ((Get-FileHash $out -Algorithm SHA256).Hash.ToLower() -ne $m.sha) { throw "checksum mismatch for $($m.file)" }
    Write-Host "ok        $($m.file)"
}
