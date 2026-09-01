// whisper-hotkeyd
//
// SPDX-License-Identifier: MIT
// Сторонние компоненты и атрибуции: см. THIRD_PARTY_NOTICES.md в корне проекта.
//
// Глобальная горячая клавиша для голосовой диктовки + оверлей статуса поверх окон.
//
// Демон владеет всем жизненным циклом диктовки:
//   - регистрирует глобальный ярлык через KGlobalAccel (по умолчанию Ctrl+E);
//   - по нажатию сам запускает/останавливает запись (parec | ffmpeg);
//   - сам вызывает whisper-cli для распознавания и парсит результат;
//   - держит состояние (запись/распознавание/покой) в памяти и переключает
//     оверлей напрямую — без промежуточного скрипта и state-файла.
//
// Ресурсы: в покое несколько МБ RAM. Модель не трогаем — whisper-cli грузит
// её только на время распознавания.

#include <QApplication>
#include <QWidget>
#include <QCursor>
#include <QAction>
#include <QKeySequence>
#include <QTimer>
#include <QProcess>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QThread>
#include <QDateTime>
#include <QtMath>
#include <chrono>

#include <cstdio>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>

#include <KGlobalAccel>

static const int kPostRollMs = 600;      // хвост после стопа: whisper дорешивает
                                         // последний слог/слово в тишине перед EOF
static const int kFinishTimeoutMs = 6000; // сколько ждём ffmpeg после остановки

// --- пути и ресурсы ------------------------------------------------------

static QString dataDir()
{
    return QDir::homePath() + QStringLiteral("/.local/share/whisper");
}

static QString modelPath()
{
    return dataDir() + QStringLiteral("/models/ggml-large-v3-turbo-q8_0.bin");
}

static QString wavPath()
{
    return dataDir() + QStringLiteral("/tmp/record.wav");
}

static QString outPath()
{
    return dataDir() + QStringLiteral("/tmp/out");
}

static QString logPath()
{
    return dataDir() + QStringLiteral("/tmp/log");
}

static QString historyPath()
{
    return dataDir() + QStringLiteral("/tmp/history.txt");
}

static void logMsg(const QString &msg)
{
    QFile f(logPath());
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")).toUtf8());
        f.write(" ");
        f.write(msg.toUtf8());
        f.write("\n");
    }
}

// Короткая фраза-подсказка: задаёт модели стиль вывода с пунктуацией.
static QString hotwordPrompt()
{
    return QStringLiteral(
        "Например, так: запятая, точка, тире — и вопрос: Как дела? Это пример пунктуации. "
        "функция метод класс переменная массив список цикл условие обработчик ошибка "
        "исключение каждого git merge branch main commit pull push async await debug "
        "define install update test");
}

// Внешний однострочный процесс без ожидания (beep, уведомления).
static void fireAndForget(const QString &program, const QStringList &args)
{
    QProcess *p = new QProcess();
    p->setProgram(program);
    p->setArguments(args);
    QObject::connect(p, &QProcess::finished, p, &QObject::deleteLater);
    p->start();
}

static void beep(const QString &name)
{
    fireAndForget(QStringLiteral("paplay"), {dataDir() + QLatin1Char('/') + name + QStringLiteral(".wav")});
}

static void notify(const QString &title, const QString &body)
{
    fireAndForget(QStringLiteral("notify-send"),
                  {QStringLiteral("-a"), QStringLiteral("Whisper"), title, body});
}

// --- оверлей ------------------------------------------------------------

class OverlayWidget : public QWidget
{
public:
    OverlayWidget()
    {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
                       | Qt::Tool | Qt::WindowDoesNotAcceptFocus);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFixedSize(170, 35);

        QTimer *anim = new QTimer(this);
        anim->setInterval(33);
        connect(anim, &QTimer::timeout, this, [this]() {
            m_phase += 0.12;
            place();
            update();
        });
        anim->start();
    }

    // false — запись, true — распознавание
    void setDisplay(bool transcribing)
    {
        if (m_transcribing == transcribing) {
            return;
        }
        m_transcribing = transcribing;
        update();
    }

protected:
    void showEvent(QShowEvent *) override
    {
        place();
    }

    void place()
    {
        if (!isVisible()) {
            return;
        }
        QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (!screen) {
            return;
        }
        const QRect g = screen->availableGeometry();
        move(g.center().x() - width() / 2,
             g.bottom() - height() - 12);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRectF pill(rect().adjusted(1, 1, -1, -1));
        p.setPen(QPen(QColor(0, 0, 0), 2));
        p.setBrush(QColor(255, 255, 255));
        p.drawRoundedRect(pill, 12, 12);

        const QPointF dot(20, height() / 2.0);

        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0));
        p.drawEllipse(dot, 13, 13);

        if (m_transcribing) {
            p.save();
            p.translate(dot);
            p.rotate(m_phase * 40.0);
            p.setPen(QPen(QColor(64, 130, 255), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(-8.5, -8.5, 17, 17), 90, 240 * 16);
            p.restore();
        } else {
            const qreal pulse = 0.5 - 0.5 * qCos(m_phase);
            p.setBrush(QColor(235, 50, 50, 150));
            p.drawEllipse(dot, 6 + pulse * 2.5, 6 + pulse * 2.5);
            p.setBrush(QColor(235, 50, 50));
            p.drawEllipse(dot, 4.5, 4.5);
        }

        auto rnd = [](qreal x) {
            const qreal v = qAbs(qSin(x * 127.1 + 311.7));
            return v - qFloor(v);
        };

        if (m_transcribing) {
            static const QString glyphs =
                QStringLiteral(
                    u"abcdefghijklmnopqrstuvwxyz"
                    u"абвгдежзиклмнопрстуфхцчшщъыьэюя"
                    u"АБВГДЕЖЗИКЛМНОПРСТУФХЦЧШЩ"
                    u"0123456789!?@#$%&*+=<>|()[]{};.,~^-_"
                    u"αβγδεζηθικλμνξοπρστυφχψω"
                    u"★☆◆◇■□▲△●○▬♥♦♣♠☺☻∞");
            const int n = glyphs.size();
            const qreal cw = 13.0;
            const int xEnd = 156;
            const int xStart = 47;
            const qreal drift = m_phase * 10.0;
            const int mHi = int(qFloor(drift / cw)) + 1;
            const int mLo = int(qCeil((drift + xStart - xEnd) / cw));

            QFont fnt = font();
            fnt.setPixelSize(11);
            fnt.setBold(true);
            p.setFont(fnt);
            p.setPen(QColor(0, 0, 0));
            const qreal cy = height() / 2.0;

            p.save();
            p.setClipRect(QRect(xStart, 0, xEnd - xStart, height()));

            for (int m = qMax(mLo, mHi - 200); m <= mHi; ++m) {
                const qreal x = xEnd + cw * m - drift;
                if (x < xStart - cw || x > xEnd + cw) continue;
                const QChar ch = glyphs.at(int(rnd(qreal(m)) * n) % n);
                p.drawText(QRectF(x, cy - 9, cw, 18),
                           Qt::AlignVCenter | Qt::AlignLeft, QString(ch));
            }
            p.restore();
            return;
        }

        const int bw = 5, gap = 3, nb = 14;
        const qreal maxH = height() - 10;
        const int x0 = 47;

        auto ampAt = [&](qreal pp) {
            const qreal kk = qFloor(pp);
            const qreal ff = pp - kk;
            return rnd(kk) + (rnd(kk + 1.0) - rnd(kk)) * ff;
        };
        auto smooth = [](qreal e, qreal a, qreal b) {
            qreal t = (e - a) / (b - a);
            t = qBound<qreal>(0.0, t, 1.0);
            return t * t * (3.0 - 2.0 * t);
        };

        const qreal offset = m_phase * 0.85;
        const qreal step = 0.5;
        for (int i = 0; i < nb; ++i) {
            const qreal pos = i * step + offset;
            const qreal coarse = ampAt(pos / 4.0);
            const qreal energy = 0.15 + 0.85 * smooth(coarse, 0.35, 0.7);
            const qreal h = 4 + maxH * energy * ampAt(pos);
            const QRectF r(x0 + i * (bw + gap), height() / 2.0 - h / 2, bw, h);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0));
            p.drawRoundedRect(r, 2, 2);
        }
    }

private:
    double m_phase = 0.0;
    bool m_transcribing = false;
};

// --- контроллер диктовки ------------------------------------------------

class DictationController : public QObject
{
    Q_OBJECT
public:
    explicit DictationController(OverlayWidget *overlay, QObject *parent = nullptr)
        : QObject(parent), m_overlay(overlay)
    {
        m_parec = new QProcess(this);
        m_ffmpeg = new QProcess(this);

        // ffmpeg финализирует файл при EOF — поэтому концовка не теряется
        m_ffmpeg->setProgram(QStringLiteral("ffmpeg"));
        m_ffmpeg->setArguments({QStringLiteral("-loglevel"), QStringLiteral("error"),
                                QStringLiteral("-f"),         QStringLiteral("s16le"),
                                QStringLiteral("-ar"),        QStringLiteral("16000"),
                                QStringLiteral("-ac"),        QStringLiteral("1"),
                                QStringLiteral("-i"),         QStringLiteral("pipe:0"),
                                QStringLiteral("-c:a"),       QStringLiteral("pcm_s16le"),
                                wavPath()});

        // Пересылаем поток parec в stdin ffmpeg вручную. Штатный
        // setStandardOutputProcess работал только первые циклы записи, затем НЕ
        // подключал пайп заново и WAV получался пустым (78 байт). Ручная
        // пересылка детерминирована при переиспользовании обоих процессов.
        m_parec->setProcessChannelMode(QProcess::SeparateChannels);
        connect(m_parec, &QProcess::readyReadStandardOutput, this, [this]() {
            m_ffmpeg->write(m_parec->readAllStandardOutput());
        });
        // EOF источника => закрываем stdin ffmpeg => ffmpeg дописывает WAV и выходит.
        // Закрываем НЕ сразу, а только когда Qt допишет в ffmpeg последний кусок
        // звука (иначе хвост фразы теряется).
        connect(m_parec, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
            m_ffmpeg->write(m_parec->readAllStandardOutput());
            flushAndCloseFfmpegInput();
        });
        connect(m_ffmpeg, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
            logMsg(QStringLiteral("[ffmpeg] finished code=%1 exit=%2")
                       .arg(code).arg(qint64(st)));
            onRecordingFinished();
        });
        connect(m_ffmpeg, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            logMsg(QStringLiteral("[ffmpeg] errorOccurred=%1").arg(qint64(e)));
        });
        connect(m_parec, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            logMsg(QStringLiteral("[parec] errorOccurred=%1").arg(qint64(e)));
        });

        // Страховка от зависания: если после стопа ffmpeg не завершится сам,
        // форсируем переход к распознаванию (не блокируя GUI waitForFinished'ом).
        m_stopDeadline = new QTimer(this);
        m_stopDeadline->setInterval(kFinishTimeoutMs);
        m_stopDeadline->setSingleShot(true);
        connect(m_stopDeadline, &QTimer::timeout, this, [this]() {
            onStopDeadline();
        });
    }

    // Вызывается по нажатию горячей клавиши: начать/остановить диктовку.
    void toggle()
    {
        switch (m_state) {
        case State::Idle:
            startRecording();
            break;
        case State::Recording:
            stopRecording();
            break;
        case State::Transcribing:
            // распознавание уже идёт — игнорируем повторное нажатие
            break;
        }
    }

signals:
    // Оверлей слушает напрямую через состояние.
    void busyChanged(bool active, bool transcribing);

private:
    enum class State { Idle, Recording, Transcribing };

    void setOverlay()
    {
        const bool active = m_state != State::Idle;
        const bool tr = m_state == State::Transcribing;
        m_overlay->setDisplay(tr);
        m_overlay->setVisible(active);
        emit busyChanged(active, tr);
    }

    void startRecording()
    {
        if (m_ffmpeg->state() != QProcess::NotRunning ||
            m_parec->state() != QProcess::NotRunning) {
            logMsg(QStringLiteral("ОШИБКА записи: предыдущий процесс ещё работает"));
            beep(QStringLiteral("beep_err"));
            notify(QStringLiteral("Ошибка записи"), QStringLiteral("Предыдущая запись ещё завершается"));
            return;
        }
        m_state = State::Recording;
        setOverlay();
        beep(QStringLiteral("beep_start"));
        notify(QStringLiteral("Запись началась"),
               QStringLiteral("Говори. Повторное нажатие — распознать текст"));

        QDir().mkpath(QDir(dataDir() + QStringLiteral("/tmp")).path());
        QFile::remove(wavPath());
        QFile::remove(outPath() + QStringLiteral(".txt"));
        logMsg(QStringLiteral("запись началась"));

        m_stopDeadline->stop();
        m_transcribeStarted = false;
        m_ffmpegClosing = false;
        // Аргументы ffmpeg заданы в конструкторе: стартуем через start() без списка.
        // Перезапись аргументов пустым списком (start("ffmpeg", {})) роняла ffmpeg
        // с code=1 сразу после старта — запись не шла, индикатор вис навсегда.
        m_ffmpeg->start();
        m_parec->start(QStringLiteral("parec"),
                       {QStringLiteral("--raw"), QStringLiteral("--format=s16le"),
                        QStringLiteral("--rate=16000"), QStringLiteral("--channels=1")});
        if (!m_ffmpeg->waitForStarted(1000) || !m_parec->waitForStarted(1000)) {
            logMsg(QStringLiteral("ОШИБКА записи: не удалось запустить parec/ffmpeg"));
            m_ffmpeg->kill();
            m_parec->kill();
            m_state = State::Idle;
            setOverlay();
            beep(QStringLiteral("beep_err"));
            notify(QStringLiteral("Ошибка записи"), QStringLiteral("Проверь parec, ffmpeg и микрофон"));
        }
    }

    void stopRecording()
    {
        // добираем хвост последних слов, потом плавно останавливаем parec
        beep(QStringLiteral("beep_stop"));
        m_state = State::Transcribing;
        setOverlay();
        notify(QStringLiteral("Распознаю..."), QString());
        logMsg(QStringLiteral("запись остановлена, распознаю..."));

        // Страховка: если ffmpeg не завершится сам по EOF (или уже стоит),
        // deadline принудительно переведёт нас в распознавание.
        m_stopDeadline->start();
        QTimer::singleShot(kPostRollMs, this, [this]() {
            // закрываем источник: parec завершается, ffmpeg дочитает пайп до EOF
            // и корректно допишет WAV (концовка не теряется).
            if (m_parec->state() != QProcess::NotRunning) {
                m_parec->terminate();
            }
        });
    }

    void onRecordingFinished()
    {
        if (m_state == State::Recording) {
            // запись оборвалась сразу (микрофон недоступен, parec/ffmpeg не
            // поднялись) — не виснуть в Recording, корректно вернуться в покой.
            m_stopDeadline->stop();
            beep(QStringLiteral("beep_err"));
            logMsg(QStringLiteral("ОШИБКА записи: ffmpeg завершился при старте"));
            m_state = State::Idle;
            setOverlay();
            notify(QStringLiteral("Ошибка записи"), QStringLiteral("Проверь микрофон"));
            return;
        }
        if (m_state != State::Transcribing) {
            return;
        }
        startTranscribing();
    }

    // Страховочный путь: ffmpeg не доложил EOF вовремя — добиваем и распознаём.
    void onStopDeadline()
    {
        if (m_state != State::Transcribing) {
            return;
        }
        if (m_ffmpeg->state() != QProcess::NotRunning) {
            m_ffmpeg->kill(); // finished от kill тоже ведёт сюда через onRecordingFinished
        }
        startTranscribing();
    }

    // Дождаться, пока Qt перепишет из своего буфера весь звук в stdin ffmpeg,
    // и только потом закрыть канал (EOF), чтобы не обрезать хвост фразы.
    void flushAndCloseFfmpegInput()
    {
        if (m_ffmpeg == nullptr || m_ffmpeg->state() == QProcess::NotRunning) {
            return; // ffmpeg уже завершился — канал закрывать некому
        }
        if (m_ffmpegClosing) {
            return;
        }
        if (m_ffmpeg->bytesToWrite() > 0) {
            m_ffmpegClosing = true;
            QTimer::singleShot(3, this, [this]() {
                m_ffmpegClosing = false;
                flushAndCloseFfmpegInput();
            });
            return;
        }
        m_ffmpeg->closeWriteChannel();
    }

    void startTranscribing()
    {
        if (m_transcribeStarted) {
            return;
        }
        m_transcribeStarted = true;
        m_stopDeadline->stop();
        archiveStart();
        if (m_audioPath.isEmpty()) {
            finishFailure();
            return;
        }
        m_outputPath = m_audioPath.left(m_audioPath.size() - QStringLiteral(".wav").size());
        transcribe();
    }

    // Архивный цикл: папка на каждую запись с аудио и текстом распознавания.
    // Нужен, чтобы при повторении проблем («обрезается конец фразы») можно было
    // сравнить WAV и TXT одного цикла и понять, где теряется звук.
    void archiveStart()
    {
        const QString base = dataDir() + QStringLiteral("/dictations");
        QDir().mkpath(base);
        m_runDir = base + QLatin1Char('/')
                   + QDateTime::currentDateTime().toString(
                       QStringLiteral("yyyyMMdd-HHmmss-zzz"));
        QDir().mkpath(m_runDir);
        m_audioPath = m_runDir + QStringLiteral("/audio.wav");
        if (!QFile::copy(wavPath(), m_audioPath)) {
            m_audioPath.clear();
        }
        logMsg(QStringLiteral("архив записи: ") + m_runDir);
    }

    void archiveText(const QString &fallback)
    {
        if (m_runDir.isEmpty()) {
            return;
        }
        QString content = fallback;
        QFile fi(m_outputPath + QStringLiteral(".txt"));
        if (fi.open(QIODevice::ReadOnly | QIODevice::Text)) {
            content = QString::fromUtf8(fi.readAll());
            fi.close();
        }
        if (content.trimmed().isEmpty() && !fallback.trimmed().isEmpty()) {
            content = fallback;
        }
        QFile fo(m_runDir + QStringLiteral("/text.txt"));
        if (fo.open(QIODevice::WriteOnly | QIODevice::Text)) {
            fo.write(content.toUtf8());
            fo.close();
        }
        m_runDir.clear();
    }

    void transcribe()
    {
        logMsg(QStringLiteral("запуск whisper-cli"));
        notify(QStringLiteral("Распознаю..."), QString());

        QProcess *w = new QProcess(this);
        w->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        w->setProgram(QStringLiteral("whisper-cli"));
        w->setArguments({QStringLiteral("-m"), modelPath(),
                         QStringLiteral("-f"), m_audioPath,
                         QStringLiteral("-l"), QStringLiteral("ru"),
                         QStringLiteral("--prompt"), hotwordPrompt(),
                         QStringLiteral("-otxt"), QStringLiteral("-of"), m_outputPath,
                         QStringLiteral("-np"), QStringLiteral("-t"),
                         QString::number(QThread::idealThreadCount())});
        connect(w, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) {
            QProcess *wdel = qobject_cast<QProcess *>(sender());
            wdel->deleteLater();
            if (exitCode != 0) {
                finishFailure();
                return;
            }
            readResult();
        });
        w->start();
    }

    void readResult()
    {
        QFile f(m_outputPath + QStringLiteral(".txt"));
        QString text;
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            text = QString::fromUtf8(f.readAll());
            text.replace(QLatin1Char('\n'), QLatin1Char(' '));
            text = text.simplified();
        }

        if (text.isEmpty()) {
            finishEmpty();
            return;
        }
        archiveText(QString());

        // в буфер обмена (wl-clipboard) и в историю
        QProcess *copy = new QProcess(this);
        copy->setProgram(QStringLiteral("wl-copy"));
        copy->start();
        copy->write(text.toUtf8());
        copy->closeWriteChannel();
        connect(copy, &QProcess::finished, copy, &QObject::deleteLater);

        QFile h(historyPath());
        if (h.open(QIODevice::Append | QIODevice::Text)) {
            h.write(text.toUtf8());
            h.write("\n");
        }

        m_state = State::Idle;
        setOverlay();
        logMsg(QStringLiteral("готово: ") + text);
        notify(QStringLiteral("Готово — Ctrl+V"), text);
        m_audioPath.clear();
        m_outputPath.clear();
    }

    void finishFailure()
    {
        beep(QStringLiteral("beep_err"));
        logMsg(QStringLiteral("ОШИБКА распознавания"));
        archiveText(QStringLiteral("ОШИБКА распознавания"));
        m_state = State::Idle;
        setOverlay();
        notify(QStringLiteral("Ошибка распознавания"), QStringLiteral("Повтори попытку"));
        m_audioPath.clear();
        m_outputPath.clear();
    }

    void finishEmpty()
    {
        archiveText(QStringLiteral("ничего не распознано"));
        m_state = State::Idle;
        setOverlay();
        logMsg(QStringLiteral("ничего не распознано"));
        notify(QStringLiteral("Ничего не распознано"),
               QStringLiteral("Повтори: нажми клавишу, говори, нажми ещё раз"));
        m_audioPath.clear();
        m_outputPath.clear();
    }

    OverlayWidget *m_overlay = nullptr;
    QProcess *m_parec = nullptr;
    QProcess *m_ffmpeg = nullptr;
    QTimer *m_stopDeadline = nullptr;
    State m_state = State::Idle;
    bool m_transcribeStarted = false;
    bool m_ffmpegClosing = false;
    QString m_runDir;
    QString m_audioPath;
    QString m_outputPath;
};

// --- main ---------------------------------------------------------------

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("whisper-hotkeyd"));
    app.setApplicationDisplayName(QStringLiteral("Whisper Dictation"));
    app.setApplicationVersion(QStringLiteral("0.2.0"));

    QKeySequence hotkey(QStringLiteral("Ctrl+E"));
    if (qEnvironmentVariableIsSet("WHISPER_HOTKEY")) {
        hotkey = QKeySequence(QString::fromLocal8Bit(qgetenv("WHISPER_HOTKEY")));
    }

    QAction *action = new QAction(&app);
    action->setObjectName(QStringLiteral("whisper-dictate"));
    action->setText(QStringLiteral("Whisper Dictation"));

    KGlobalAccel::self()->setDefaultShortcut(action, QList<QKeySequence>{hotkey});
    KGlobalAccel::self()->setShortcut(action, QList<QKeySequence>{hotkey});

    OverlayWidget overlay;
    DictationController controller(&overlay);

    QObject::connect(action, &QAction::triggered, &controller, &DictationController::toggle);

    std::fprintf(stderr, "whisper-hotkeyd %s: hotkey=%s model=%s (idle: ~few MB RAM)\n",
                 app.applicationVersion().toUtf8().constData(),
                 hotkey.toString().toUtf8().constData(),
                 modelPath().toUtf8().constData());
    std::fflush(stderr);

    return app.exec();
}

#include "whisper-hotkeyd.moc"
