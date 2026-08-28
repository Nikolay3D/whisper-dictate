# whisper-hotkeyd

Демон горячей клавиши: слушает комбинацию (по умолчанию `Ctrl+E`) и по нажатию
запускает скрипт `whisper-dictate` (первое нажатие — запись, второе — распознавание).
Пока идёт запись, показывает внизу экрана анимацию «волны голоса» поверх всех окон.

Модель не загружает — этим занимается `whisper-dictate` только на время распознавания.
В покое демон занимает несколько МБ RAM (без него глобальная клавиша невозможна).

## Требования

- Arch Linux, KDE/Plasma (Wayland), рабочий `org.kde.kglobalacceld`
- пакеты: `whisper-cpp`, `ggml-cpu`/`ggml-vulkan`, `wl-clipboard`, `ffmpeg` (для записи)
- dev-зависимости для сборки: `qt6-base`, `kguiaddons`? — достаточно:
  `base-devel cmake qt6-base kglobalaccel` (заголовочные файлы)

## Сборка и установка

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build          # -> /usr/local/bin/whisper-hotkeyd (+ .desktop)
# или просто:
install -m755 build/whisper-hotkeyd ~/.local/bin/
```

## Автозапуск

Скопировать `whisper-hotkeyd.desktop` в `~/.config/autostart/`.

## Настройка

- Комбинация клавиш: переменная окружения `WHISPER_HOTKEY` (например `Ctrl+Alt+W`),
  либо поменять в Системных настройках → Клавиатура → Клавиши → «Whisper Dictation».
- Путь к скрипту: переменная `WHISPER_SCRIPT` (по умолчанию `~/.local/bin/whisper-dictate`).

## Ограничения

- `Ctrl+E` становится глобальным и перехватывается у приложений — при конфликте выбери
  другую комбинацию.
- «Поверх всех окон» работает для обычных окон KWin; поверх полноэкранных игр
  (с exclusиvn-захватом) могут понадобиться слои (wlr-layer-shell) — вне рамок задачи.