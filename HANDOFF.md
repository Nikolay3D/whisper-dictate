# HANDOFF — перенос всей логики диктовки в демон C++ (whisper-hotkeyd)

## Текущее состояние (демон 0.2.0)
- Демон `src/whisper-hotkeyd.cpp` переписан: вся логика диктовки (запись parec|ffmpeg,
  распознавание whisper-cli, состояние idle/recording/transcribing, оверлей) живёт в C++.
- Скрипт `kit/whisper-dictate` БОЛЬШЕ НЕ вызывается демоном (по решению пользователя).
- Демон СЕЙЧАС ЗАПУЩЕН: PID 1707210, бинарь в ~/.local/bin/whisper-hotkeyd,
  stderr в ~/.local/share/whisper/tmp/hotkeyd.err (демон жив, крашей нет).
- Сборка: `cmake -S src -B src/build -DCMAKE_BUILD_TYPE=Release && cmake --build src/build`.
  Установка: `install -m755 src/build/whisper-hotkeyd ~/.local/bin/whisper-hotkeyd`.

## КЛЮЧЕВАЯ НЕРЕШЁННАЯ ПРОБЛЕМА (главное!)
Кажется, распознавание не запускается после остановки записи:
- В логе ~/.local/share/whisper/tmp/log появилось «14:26:16 запись началась» и
  «14:26:20 запись остановлена, распознаю...», НО строки «запуск whisper-cli»
  и «готово: ...» НЕ появились; whisper-cli не поднялся, результат нет.
- Эту сессию сделал демон старой сборки (PID 1690100, до добавления диагностики).
- Чтобы понять точку обрыва, добавлены log-точки:
  * `[ffmpeg] finished code=... exit=...` и `[ffmpeg] errorOccurred=...` / `[parec] errorOccurred=...`
  * `[dbg] onRecordingFinished, state=...` и `[dbg] пропуск: state не Transcribing`
  * `[dbg] оба процесса стоят, whisper не поднят` (таймер раз в 2с, если оба NotRunning в Transcribing)
- НУЖНО: попросить пользователя нажать Ctrl+E дважды (запись→стоп) и посмотреть в
  ~/.local/share/whisper/tmp/log, где обрывается (особенно есть ли «запуск whisper-cli»).

## Главные подозрения на причину зависания
1. БЛОКИРУЮЩИЙ `m_ffmpeg->waitForFinished(kFinishTimeoutMs)` в stopRecording() —
   вызывается в GUI-потоке. Сильный кандидат на зависание. Рекомендация: убрать
   блокирующий wait, сделать полностью асинхронный поток:
   `parec->terminate()` -> ждать `ffmpeg finished` сигнал -> transcribe().
2. `setStandardOutputProcess` требует запуска destination (ffmpeg) ПЕРЕД source (parec).
   В коде ffmpeg стартуется первым — ок. Но проверить, реально ли завершается ffmpeg
   по EOF и шлёт ли `finished`.
3. Возможно `m_state` в момент finished не Transcribing (тогда «пропуск» — но его нет в логе).

## Поток записи/распознавания в демоне (методы)
- toggle(): Idle->startRecording(), Recording->stopRecording(), Transcribing->игнор.
- startRecording(): beep_start, notify, ffmpeg->start("ffmpeg",{args pipe:0}), parec->start("parec",{--raw...}).
  ffmpeg args: -loglevel error -f s16le -ar 16000 -ac 1 -i pipe:0 -c:a pcm_s16le <wav>
- stopRecording(): beep_stop, state=Transcribing, notify, QTimer kPostRollMs(220мс) затем
  parec->terminate(); ffmpeg->waitForFinished(6000); при неуспехе ffmpeg->kill().
- onRecordingFinished(): если state==Transcribing -> transcribe().
- transcribe(): whisper-cli -m model -f wav -l ru --prompt hotwordPrompt() -otxt -of out
  -np -t nproc; finished(0) -> readResult().
- readResult(): парсит out.txt, wl-copy, история, notify «Готово — Ctrl+V».
- finishFailure/finishEmpty: beep_err/notify, state=Idle.

## Пути и ресурсы (проверены, есть на машине)
- dataDir = ~/.local/share/whisper
- model = ~/.local/share/whisper/models/ggml-large-v3-turbo-q8_0.bin
- wav = ~/.local/share/whisper/tmp/record.wav; out/txt = ~/.local/share/whisper/tmp/out.txt
- beeps: beep_start/stop/err.wav в dataDir; wl-copy, notify-send, paplay, whisper-cli есть.
- ffmpeg с `-i pipe:0 -c:a pcm_s16le` корректно финализирует WAV по EOF (проверено).

## Известные баги, уже исправленные ранее
- Обрезание концовки: причина была в осиротевших parec|ffmpeg, писавших в один WAV.
  Теперь демон сам одним экземпляром владеет записью через setStandardOutputProcess.
- Накопление процессов / state-файл больше не используются (всё в памяти демона).
- Пунктуация: whisper large-v3-turbo по-русски ставит пунктуацию плохо; промпт-затравка
  добавлена (hotwordPrompt()), полное решение — пост-пунктуатор или NeMo parakeet (НЕ сделано).

## Следующие шаги (порядок)
1. ПОФИКСИТЬ зависание распознавания (см. подозрения выше; вероятнее всего убрать
   блокирующий waitForFinished).
2. Пересобрать, установить, перезапустить демон.
3. Смоук-цикл: Ctrl+E (запись) -> Ctrl+E (стоп) -> в логе «готово: ...» + текст в буфере.
4. Обновить AGENTS.md (демон владеет жизненным циклом, скрипт не нужен) и отметить автостарт.

## Авторы
- demo-перезапуск: `pkill -x whisper-hotkeyd; sleep 1; nohup setsid ~/.local/bin/whisper-hotkeyd >~/.local/share/whisper/tmp/hotkeyd.err 2>&1 &`
