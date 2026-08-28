#!/usr/bin/env bash
# uninstall.sh — полное удаление Whisper-диктовки
set -euo pipefail

SCRIPT_NAME="whisper-dictate"
MODEL_DIR="$HOME/.local/share/whisper"
SCRIPT_PATH="$HOME/.local/bin/$SCRIPT_NAME"
BIN_MODEL="$MODEL_DIR/models/ggml-large-v3-turbo-q8_0.bin"
BIN_BEEPS="$MODEL_DIR/beep_start.wav $MODEL_DIR/beep_stop.wav $MODEL_DIR/beep_err.wav"
BIN_TMP="$MODEL_DIR/tmp"

echo "==> Удаляю пользовательские файлы"
echo "      - $SCRIPT_PATH"
rm -f "$SCRIPT_PATH"
echo "      - $BIN_MODEL"
echo "      - $BIN_BEEPS"
echo "      - $BIN_TMP (записи, текст, история)"
echo "      - $MODEL_DIR (вся папка)"
rm -rf "$MODEL_DIR"

echo "==> Проверяю активный процесс (если запись шла — останавливаю)"
pkill -f "$SCRIPT_NAME" 2>/dev/null && echo "      остановлен: $SCRIPT_NAME" || true
pkill -f "parec.*16000" 2>/dev/null && echo "      остановлена запись (parec)" || true
rm -f "${XDG_RUNTIME_DIR:-/tmp}/whisper-dictate.pid"
echo "      удалён pid-файл: ${XDG_RUNTIME_DIR:-/tmp}/whisper-dictate.pid"

echo "==> Пакеты из репо Arch (удаляются из /usr/bin, /usr/lib)"
echo "      пакеты: whisper-cpp ggml-cpu ggml-vulkan wl-clipboard"
read -r -p "Удалить эти пакеты? [y/N] " ans
if [ "${ans:-n}" = "y" ] || [ "${ans:-n}" = "Y" ]; then
    sudo pacman -Rns whisper-cpp ggml-cpu ggml-vulkan wl-clipboard
fi

echo "Готово."