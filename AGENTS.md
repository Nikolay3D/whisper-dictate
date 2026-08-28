# AGENTS.md — руководство для агентов в этом репозитории

## Проект

`whisper-dictate` — локальная голосовая диктовка на «кнопке» для **Arch Linux / KDE Plasma (Wayland)**.
Нажал горячую клавишу → говоришь → нажал → текст в буфере обмена. Русский + английские кодовые термины.
Работает полностью офлайн.

Схема: `whisper-hotkeyd` (глобальный ярлык Ctrl+E через KGlobalAccel) запускает `whisper-dictate`
(скрипт записи/распознавания), тот держит в памяти модель **только** на время распознавания.

## Структура

```
whisper-dictate/
├── kit/            — дистрибутивный набор (то, что раскатывается на машины)
│   ├── whisper-dictate   — главный скрипт «кнопки» (запись + распознавание + статус)
│   ├── install.sh        — установка: пакеты, директории, скрипт, звуки, модель
│   ├── uninstall.sh      — удаление
│   ├── README.md         — пользовательская инструкция
│   └── beep_*.wav        — звуки старт/стоп/ошибка
├── src/            — исходники демона
│   ├── whisper-hotkeyd.cpp  — Qt6/KF6: глобальный ярлык + оверлей записи/распознавания
│   ├── CMakeLists.txt       — сборка demoна, цель `whisper-hotkeyd`
│   └── whisper-hotkeyd.desktop — автозапуск (копируется в ~/.config/autostart)
├── LICENSE                — MIT (собственный код)
├── THIRD_PARTY_NOTICES.md — лицензии используемых компонентов
└── README.md              — карта проекта и сборка
```

## Ключевые точки интеграции

- **Статус-файл** `~/.local/share/whisper/tmp/state` — значения `recording` / `transcribing` / `idle`.
  Пишет `kit/whisper-dictate`, читает демон каждые 400 мс и переключает оверлей. Не сломать инвариант:
  скрипт обязан выставить `idle` на всех путях завершения.
- **Активность записи**: pid-файл `${XDG_RUNTIME_DIR:-/tmp}/whisper-dictate.pid` (PID группы setsid
  из `parec | ffmpeg`). У демона есть фолбэк на pid-файл, если state-файла нет (старый скрипт).
- **Ограничение записи**: `MAX_RECORD_SECONDS=${WHISPER_MAX_SECONDS:-300}` — автопрерывание записи.
- **Модель**: `~/.local/share/whisper/models/ggml-large-v3-turbo-q8_0.bin` (874 МБ, gguf q8_0).
  Путь зашит в `kit/whisper-dictate`. Распознавание: `whisper-cli -m ... -l ru --prompt "$HOTWORDS" -otxt -of ... -np -t $(nproc)`.

## Команды

```sh
# Сборка демона
cmake -B src/build -DCMAKE_BUILD_TYPE=Release && cmake --build src/build

# Развернуть себе локально
install -m755 src/build/whisper-hotkeyd "$HOME/.local/bin/"
cp kit/whisper-dictate "$HOME/.local/bin/"   # chmod +x

# Проверка синтаксиса скриптов
bash -n kit/whisper-dictate kit/install.sh kit/uninstall.sh
```

Про «тестов»: автотестов нет. Ручной смоук-цикл: запустить `~/.local/bin/whisper-dictate`,
подождать ~1 с, запустить ещё раз (стоп/распознавание), убедиться, что в
`~/.local/share/whisper/tmp/log` появились `запись началась` → `запись остановлена, распознаю...`
→ `готово: ...` (или `ничего не распознано`), и в `tmp/state` вернулось `idle`.

## Соглашения

- Язык комментариев/сообщений: русский (UTF-8).
- Baш: `set -euo pipefail`; шум в stderr глушим `2>/dev/null` с `|| true` там, где не критично.
- C++: Qt6 + KF6, C++17; окно оверлея frameless + WindowStaysOnTopHint.
- Без новых зависимостей сверх уже используемых (whisper-cpp, ggml, wl-clipboard, ffmpeg, parec,
  Qt6, KF6 GlobalAccel). Добавление — только по явному запросу.
- Изменение путей модели/статуса — синхронно в `kit/whisper-dictate` и `src/whisper-hotkeyd.cpp`.
- Кит и установленная копия должны совпадать: после правок `kit/whisper-dictate` → `cp` в `~/.local/bin/`.
- Логи/сообщения пользователю — краткие и понятные, состояние показывается и звуком, и уведомлением KDE.

## Git

- ASCII-инциденты: названия коммитов по-русски короткие и по делу.
- Автор: `-c user.name=nikolay -c user.email=nikolay@local` (глобально в системе не настроено).
- Remote origin: `git@github.com:Nikolay3D/whisper-dictate.git` (SSH; публичный ключ id_ed25519 уже зарегистрирован).
- Не коммитить ранее явного запроса.

## Известные ограничения / TODO

- «Поверх всех окон» — для обычных окон KWin; поверх полноэкранных игр с исключительным захватом
  не работает (нужен wlr-layer-shell).
- Автовставка текста в активное окно (`ydotool`) — запланировано, не сделано.
- В комите установки `kit/install.sh` демон пока не ставится/не регистрируется на автозапуск.