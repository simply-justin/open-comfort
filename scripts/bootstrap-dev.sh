#!/usr/bin/env bash
set -euo pipefail

readonly IDF_VERSION="${IDF_VERSION:-v5.5.1}"
readonly IDF_ROOT="${OPENCOMFORT_IDF_PATH:-${HOME}/.espressif/frameworks/esp-idf-${IDF_VERSION}}"

if [[ ! -f "${IDF_ROOT}/export.sh" ]]; then
  mkdir -p "$(dirname "${IDF_ROOT}")"
  git clone \
    --branch "${IDF_VERSION}" \
    --depth 1 \
    --recursive \
    --shallow-submodules \
    https://github.com/espressif/esp-idf.git \
    "${IDF_ROOT}"
fi

"${IDF_ROOT}/install.sh" esp32s3

cat <<EOF

ESP-IDF ${IDF_VERSION} is installed.
Activate it in this shell with:

  source "${IDF_ROOT}/export.sh"

Then configure and build OpenComfort:

  idf.py set-target esp32s3
  idf.py build
EOF
