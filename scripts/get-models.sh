#!/usr/bin/env bash
# Downloads the two speech models into models/ and checks them. Safe to run again.
set -euo pipefail
dir="$(cd "$(dirname "$0")/../models" && pwd)"
fetch() {  # repo revision file sha256
  local out="$dir/$3"
  if [ -f "$out" ] && [ "$(shasum -a 256 "$out" | cut -d' ' -f1)" = "$4" ]; then echo "ok        $3"; return; fi
  echo "download  $3"
  curl -fL --retry 3 -o "$out" "https://huggingface.co/$1/resolve/$2/$3"
  [ "$(shasum -a 256 "$out" | cut -d' ' -f1)" = "$4" ] || { echo "checksum mismatch for $3" >&2; exit 1; }
  echo "ok        $3"
}
fetch nvidia/parakeet-tdt-0.6b-v3 541d1f99c6b0c3cd0b11a95167540bb8edefd82b \
  parakeet-tdt-0.6b-v3.q8_0.gguf e3880d0aaaaf2c308ea2c35016b2b895c423eb3fda924c1b463d1c19b7f4d32e
fetch nvidia/Nemotron-3-Diarization f667ed73aee57d40cc39428eb768b4fd87a0a29e \
  Nemotron-3-Diarization.q8_0.gguf 08456d9e22cd9a323c0364d98375f3746d6e68507ebb705cd46438c534c7a3a1
