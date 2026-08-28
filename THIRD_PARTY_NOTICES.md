# THIRD PARTY NOTICES

Проект `whisper-dictate` использует следующие сторонние компоненты.
Этот файл — обязательная атрибуция, требуемая их лицензиями.

Важно: проект **не распространяет** бинарников/исходников этих компонентов —
они устанавливаются из официальных репозиториев Arch Linux или скачиваются
распространителем. Настоящие уведомления остаются в силе при распространении
или использовании данного проекта.

## Сводка

| Компонент | Лицензия | Как используется |
|---|---|---|
| whisper.cpp (движок `whisper-cli`) | MIT | запускается как отдельный процесс |
| GGML (вычислительная библиотека) и бэкенды `ggml-cpu`/`ggml-vulkan` | MIT | встроены в `whisper-cli` |
| Модель Whisper `ggml-large-v3-turbo-q8_0.bin` (OpenAI Whisper large-v3-turbo, квантование q8_0) | MIT | файл модели, загружается `whisper-cli` |
| Qt 6 (`qt6-base`, динамическая линковка) | LGPL-3.0 / GPL-3.0 | `whisper-hotkeyd` слинкован динамически |
| KDE Frameworks: `kglobalaccel`, `kguiaddons` (динамическая линковка) | LGPL-2.0 / LGPL-3.0 | `whisper-hotkeyd` (глобальный ярлык) |
| `wl-clipboard` (`wl-copy`) | GPL-3.0-or-later | внешняя утилита, копирует текст в буфер |
| `pulseaudio`/PipeWire `parec` | LGPL-2.1 | внешняя утилита записи с микрофона |
| `ffmpeg` | GPL-3.0 (сборка Arch) | внешняя утилита, конвертация wav |

## MIT-компоненты (требование: сохранить уведомление о копирайте)

### whisper.cpp / GGML

Файл лицензии системы: `/usr/share/licenses/whisper-cpp/LICENSE` и `/usr/share/licenses/ggml/LICENSE`

```
MIT License

Copyright (c) 2023-2026 The ggml authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### Модель Whisper (OpenAI)

Веса модели Whisper (включая large-v3-turbo) распространяются OpenAI под MIT.
Код и веса: https://github.com/openai/whisper — `Copyright (c) 2022 OpenAI`
(лицензия MIT, текст аналогичен приведённому выше). Квантованный файл
`ggml-large-v3-turbo-q8_0.bin` — результат конвертации этих весов в формат GGML.

## LGPL / GPL-компоненты (примечания к соблюдению)

- **Qt 6 (LGPL-3.0).** `whisper-hotkeyd` использует Qt только через динамическую
  линковку (`libQt6*`). Это соответствует LGPL: пользователь может заменить
  библиотеки Qt своей сборкой. Исходники Qt доступны у Arch/проекта Qt.
  Никаких изменённых библиотек Qt проект не распространяет.
- **KF6 GlobalAccel (LGPL).** То же — динамическая линковка, модификации не вносились.
- **wl-clipboard, ffmpeg (GPL/LGPL с опцией GPL).** Запускаются как внешние процессы
  по системному пути; их код/бинарники в проект не включаются. При самостоятельной
  дистрибуции собранных бинарников этих утилит распространяет GPL-обязательства их
  сборки.

## Собственный код

Код проекта (`whisper-hotkeyd.cpp`, `whisper-dictate`, скрипты установки) —
оригинальная разработка, лицензируется по MIT (см. `LICENSE`).