// whisper-hotkeyd
//
// Глобальная горячая клавиша для whisper-dictate + анимация записи поверх окон.
//
// Работа:
//   - регистрирует глобальный ярлык через KGlobalAccel (по умолчанию Ctrl+E);
//   - по нажатию запускает скрипт whisper-dictate (он сам решает: начать/остановить
//     запись и распознать);
//   - пока запись активна (по pid-файлу) показывает анимацию голоса внизу экрана.
//
// Ресурсы: в покое несколько МБ RAM. Модель не трогает — её грузит whisper-dictate
// только на время распознавания.

#include <QApplication>
#include <QWidget>
#include <QAction>
#include <QKeySequence>
#include <QTimer>
#include <QProcess>
#include <QScreen>
#include <QPainter>
#include <QPainterPath>
#include <QDir>
#include <QFile>
#include <QtMath>
#include <chrono>

#include <cstdio>
#include <csignal>
#include <sys/types.h>
#include <unistd.h>

#include <KGlobalAccel>

static const int kPollMs = 400;
static const int kWaveBars = 28;

// --- helpers ------------------------------------------------------------

static QString scriptPath()
{
    const QByteArray env = qgetenv("WHISPER_SCRIPT");
    if (!env.isEmpty()) {
        return QString::fromLocal8Bit(env);
    }
    return QDir::homePath() + QStringLiteral("/.local/bin/whisper-dictate");
}

static QString pidFilePath()
{
    const QString base = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (!base.isEmpty()) {
        return base + QStringLiteral("/whisper-dictate.pid");
    }
    return QStringLiteral("/tmp/whisper-dictate.pid");
}

// Идёт ли запись: жив ли процесс, который сохранил pid-файл (группа setsid).
static bool recordingActive()
{
    QFile f(pidFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    bool ok = false;
    const pid_t pid = f.readAll().trimmed().toLongLong(&ok);
    if (!ok || pid <= 0) {
        return false;
    }
    return ::kill(-pid, 0) == 0 || ::kill(pid, 0) == 0;
}

// Статус из whisper-dictate: recording / transcribing / idle.
static QString stateFilePath()
{
    return QDir::homePath() + QStringLiteral("/.local/share/whisper/tmp/state");
}

static QString readState()
{
    QFile f(stateFilePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return QStringLiteral("idle");
    }
    return QString::fromUtf8(f.readAll().trimmed());
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
        setFixedSize(380, 96);

        QTimer *anim = new QTimer(this);
        anim->setInterval(33);
        connect(anim, &QTimer::timeout, this, [this]() {
            m_phase += 0.35;
            update();
        });
        anim->start();
    }

    // false — «Запись...», true — «Распознаю...»
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
        if (QScreen *screen = QGuiApplication::primaryScreen()) {
            const QRect g = screen->availableGeometry();
            move(g.center().x() - width() / 2,
                 g.bottom() - height() - 16);
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        // фон-«таблетка»
        QRectF pill(rect().adjusted(1, 1, -1, -1));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(18, 18, 24, 238));
        p.drawRoundedRect(pill, 22, 22);
        p.setBrush(QColor(255, 255, 255, 8));
        p.drawRoundedRect(pill.adjusted(0, 0, 0, -pill.height() / 2), 22, 22);

        const QPointF dot(28, height() / 2.0);

        if (m_transcribing) {
            // оранжевый спиннер распознавания
            p.save();
            p.translate(dot);
            p.rotate(m_phase * 40.0);
            p.setPen(QPen(QColor(255, 190, 64), 3.5, Qt::SolidLine, Qt::RoundCap));
            p.drawArc(QRectF(-9.5, -9.5, 19, 19), 0, 110 * 16);
            p.restore();
        } else {
            // пульсирующая красная точка записи
            const qreal pulse = 0.5 + 0.5 * qAbs(qSin(m_phase));
            p.setBrush(QColor(255, 64, 64, 120));
            p.drawEllipse(dot, 9 + pulse * 4, 9 + pulse * 4);
            p.setBrush(QColor(255, 64, 64));
            p.drawEllipse(dot, 6, 6);
        }

        // текст
        QFont f = font();
        f.setPointSize(10);
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(255, 255, 255));
        p.drawText(QRect(48, 10, 150, height() - 20),
                   Qt::AlignVCenter | Qt::AlignLeft,
                   m_transcribing ? QStringLiteral("Распознаю...")
                                  : QStringLiteral("Запись..."));

        // волна голоса
        const qreal baseH = m_transcribing ? 12.0 : 24.0;
        const int bw = 6, gap = 4, nb = kWaveBars;
        const int totalW = nb * bw + (nb - 1) * gap;
        const int x0 = width() - totalW - 24;
        for (int i = 0; i < nb; ++i) {
            const qreal h = 6 + baseH * (0.5 + 0.5 * qAbs(qSin(m_phase * (m_transcribing ? 0.6 : 1.1) + i * 0.45)));
            const QRectF bar(x0 + i * (bw + gap), height() / 2.0 - h / 2, bw, h);
            p.setPen(Qt::NoPen);
            p.setBrush(m_transcribing ? QColor(255, 190, 64) : QColor(70, 205, 115));
            p.drawRoundedRect(bar, 3, 3);
        }
    }

private:
    double m_phase = 0.0;
    bool m_transcribing = false;
};

// --- main ---------------------------------------------------------------

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("whisper-hotkeyd"));
    app.setApplicationDisplayName(QStringLiteral("Whisper Dictation"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

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

    auto refreshOverlay = [&]() {
        const QString st = readState();
        bool transcribing = false;
        bool active = false;
        if (st == QLatin1String("recording")) {
            active = true;
        } else if (st == QLatin1String("transcribing")) {
            active = true;
            transcribing = true;
        } else {
            active = recordingActive();   // fallback: pid-файл (старый скрипт)
        }
        if (active) {
            overlay.setDisplay(transcribing);
        }
        overlay.setVisible(active);
    };

    QObject::connect(action, &QAction::triggered, [&]() {
        QProcess::startDetached(scriptPath());
        QTimer::singleShot(std::chrono::milliseconds(250), &overlay, [&]() {
            refreshOverlay();
        });
    });

    QTimer *poll = new QTimer(&app);
    QObject::connect(poll, &QTimer::timeout, refreshOverlay);
    poll->start(kPollMs);

    std::fprintf(stderr,
                 "whisper-hotkeyd: hotkey=%s script=%s (idle: ~few MB RAM)\n",
                 hotkey.toString().toUtf8().constData(),
                 scriptPath().toUtf8().constData());
    std::fflush(stderr);

    return app.exec();
}