#!/usr/bin/env bash
# install.sh — установка Whisper-диктовки (Arch Linux, KDE/Plasma, Wayland)
# Перeчень файлов набора: whisper-dictate, beep_*.wav, README.md
set -euo pipefail

SCRIPT_NAME="whisper-dictate"
BEEPS="beep_start.wav beep_stop.wav beep_err.wav"
MODEL_URL="https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q8_0.bin"
MODEL_FILE="$HOME/.local/share/whisper/models/ggml-large-v3-turbo-q8_0.bin"
MODEL_SIZE_EXPECTED=874188075

cd "$(dirname "$0")"   # работаем из директории набора

pkg_list="whisper-cpp ggml-cpu ggml-vulkan wl-clipboard"

echo "==> 1/4 Пакеты из официальных репо Arch (потребуется sudo)"
echo "    устанавливаются в систему (см. /usr/bin, /usr/lib):"
for p in $pkg_list; do
    echo "      [$p]"
done
sudo pacman -S --needed $pkg_list

echo
echo "==> 2/4 Директории пользователя"
mkdir -p "$HOME/.local/bin" "$HOME/.local/share/whisper/models" "$HOME/.local/share/whisper/tmp"
echo "      создано:  $HOME/.local/bin"
echo "                $HOME/.local/share/whisper/models"
echo "                $HOME/.local/share/whisper/tmp"

echo
echo "==> 3/4 Скрипт и звуки"
install -m 755 "$SCRIPT_NAME" "$HOME/.local/bin/$SCRIPT_NAME"
echo "      $PWD/$SCRIPT_NAME  ->  $HOME/.local/bin/$SCRIPT_NAME"
for b in $BEEPS; do
    install -m 644 "$b" "$HOME/.local/share/whisper/$b"
    echo "      $PWD/$b  ->  $HOME/.local/share/whisper/$b"
done

echo
echo "==> 4/4 Модель ($(( MODEL_SIZE_EXPECTED / 1024 / 1024 )) МБ)"
echo "      ->  $MODEL_FILE"
if [ -s "$MODEL_FILE" ] && [ "$(stat -c%s "$MODEL_FILE")" -eq "$MODEL_SIZE_EXPECTED" ]; then
    echo "    уже скачана, пропускаю."
else
    curl -L --fail --progress-bar -o "$MODEL_FILE" "$MODEL_URL"
    [ "$(stat -c%s "$MODEL_FILE")" -eq "$MODEL_SIZE_EXPECTED" ] || { echo "    ОШИБКА: размер файла не совпал"; rm -f "$MODEL_FILE"; exit 1; }
fi

echo
echo "Готово. Проверка:"
echo "  1-й запуск:  $HOME/.local/bin/$SCRIPT_NAME   (бип — говори)"
echo "  2-й запуск:  $HOME/.local/bin/$SCRIPT_NAME   (бип — идет распознавание)"
echo "  Результат — в буфере обмена (Ctrl+V) и в уведомлении KDE."
echo "  Подробно: README.md"