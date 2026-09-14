#include "skiapdfviewerwidget.h"
#include "pdfviewerwidget.h"
#include "pdfdocument.h"

#include <QApplication>
#include <QDebug>
#include <QImage>
#include <QMainWindow>
#include <QThread>
#include <QElapsedTimer>
#include <QSet>
#include <QColor>
#include <QMouseEvent>

namespace {

void pumpFor(int ms) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 16);
        // processEvents takes at least the event delay; yield a bit.
        QThread::msleep(8);
    }
}

struct FbSummary {
    QColor avg;
    int distinct = 0;
    QRect bbox;
    qint64 nonBg = 0;
    QColor dominant;
};

FbSummary summarize(const QImage& img) {
    FbSummary s;
    if (img.isNull()) return s;
    double r = 0, g = 0, b = 0;
    QHash<QRgb, int> freq;
    int minX = img.width(), minY = img.height(), maxX = 0, maxY = 0;
    QRgb dom = QRgb(0); int domN = -1;
    // Sample every 4th pixel to keep it fast.
    for (int y = 0; y < img.height(); y += 4) {
        for (int x = 0; x < img.width(); x += 4) {
            QRgb c = img.pixel(x, y);
            r += qRed(c); g += qGreen(c); b += qBlue(c);
            int n = ++freq[c];
            if (n > domN) { domN = n; dom = c; }
        }
    }
    const qint64 samples = img.width() / 4 * (img.height() / 4) + 1;
    s.avg = QColor(int(r / samples), int(g / samples), int(b / samples));
    s.dominant = dom;
    s.distinct = freq.size();
    // bbox / nonBg count relative to the most common color.
    for (int y = 0; y < img.height(); y += 4) {
        for (int x = 0; x < img.width(); x += 4) {
            QRgb c = img.pixel(x, y);
            if (c != dom) {
                ++s.nonBg;
                minX = qMin(minX, x); maxX = qMax(maxX, x);
                minY = qMin(minY, y); maxY = qMax(maxY, y);
            }
        }
    }
    if (s.nonBg > 0) s.bbox = QRect(minX, minY, maxX - minX, maxY - minY);
    return s;
}

void report(const QString& tag, const QImage& fb) {
    FbSummary s = summarize(fb);
    qDebug().noquote() << "[PROBE] " << tag
                       << "size=" << fb.size()
                       << "avg=(" << s.avg.red() << s.avg.green() << s.avg.blue() << ")"
                       << "distinct=" << s.distinct
                       << "nonBg=" << s.nonBg
                       << "bbox=" << (s.nonBg ? s.bbox : QRect(0, 0, 0, 0));
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QString pdfPath = argc > 1 ? QString::fromUtf8(argv[1]) : "/tmp/skia_test.pdf";
    const bool onlySelection = argc > 2 && QString::fromUtf8(argv[2]) == "sel" ||
                                (argc > 2 && QString::fromUtf8(argv[2]) == "d");

    if (onlySelection) goto selScenario;

    // ------------------------------------------------------------------
    // Scenario A: "open first, then toggle" (mirrors the real app: CPU viewer
    // loads, GPU viewer sits idle, then View -> GPU Acceleration).
    // ------------------------------------------------------------------
    {
        QMainWindow win;
        win.resize(1024, 768);
        win.show();

        PdfDocument* doc = new PdfDocument(&win);
        PdfViewerWidget* cpu = new PdfViewerWidget(&win);
        SkiaPdfViewerWidget* gpu = new SkiaPdfViewerWidget(&win);
        cpu->setDocument(doc);
        gpu->setDocument(doc);
        win.setCentralWidget(cpu);

        qDebug().noquote() << "[PROBE] A: load via CPU viewer";
        bool ok = cpu->loadFile(pdfPath);
        qDebug().noquote() << "[PROBE] A: load ok=" << ok
                           << "cpu page/zoom=" << cpu->currentPage() << cpu->zoom()
                           << "gpu page/zoom=" << gpu->currentPage() << gpu->zoom();
        pumpFor(1500);
        cpu->grab().save("/tmp/real_cpu.png");
        qDebug().noquote() << "[PROBE] A: saved /tmp/real_cpu.png"
                           << "cpu grab size=" << cpu->grab().size()
                           << "dpr=" << cpu->devicePixelRatioF();

        qDebug().noquote() << "[PROBE] A: simulate toggle to GPU";
        const int srcPage = cpu->currentPage();
        const qreal srcZoom = cpu->zoom();
        const int srcRot = cpu->rotation();
        const int srcMode = static_cast<int>(cpu->viewMode());
        gpu->setPage(srcPage);
        gpu->setZoom(srcZoom);
        gpu->setRotation(srcRot);
        gpu->setViewMode(srcMode == 0 ? AbstractPdfViewer::ViewMode::SinglePage
                                      : AbstractPdfViewer::ViewMode::Continuous);
        gpu->setTextSelectionEnabled(true);
        win.setCentralWidget(gpu);
        gpu->show();
        gpu->raise();
        pumpFor(5000);

        // [PROBE] D2: click on page text in GPU selection mode (shared doc).
        {
            QRectF pr2 = gpu->pageRect();
            QPoint click2 = pr2.center().toPoint();
            qDebug().noquote() << "[PROBE] A-D2: click at" << click2
                               << "pageRect=" << pr2.toRect();
            QMouseEvent press2(QEvent::MouseButtonPress, QPointF(click2),
                               Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent rel2(QEvent::MouseButtonRelease, QPointF(click2),
                             Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &press2);
            QApplication::sendEvent(gpu, &rel2);
            pumpFor(400);
            qDebug().noquote() << "[PROBE] A-D2: click done, alive";

            // Double-click (word select) + contiguous view.
            QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(click2),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &dbl);
            QApplication::sendEvent(gpu, &rel2);
            pumpFor(300);
            qDebug().noquote() << "[PROBE] A-D2: dblclick done, alive";
            gpu->setViewMode(AbstractPdfViewer::ViewMode::Continuous);
            pumpFor(300);
            QApplication::sendEvent(gpu, &press2);
            QApplication::sendEvent(gpu, &rel2);
            pumpFor(300);
            qDebug().noquote() << "[PROBE] A-D2: continuous click done, alive";
        }

        QImage gpuGrab = gpu->grabFramebuffer();
        gpuGrab.save("/tmp/real_gpu.png");
        qDebug().noquote() << "[PROBE] A: saved /tmp/real_gpu.png"
                           << "gpu grab size=" << gpuGrab.size()
                           << "dpr=" << gpu->devicePixelRatioF()
                           << "pageRect=" << gpu->pageRect().toRect();
        qDebug().noquote() << "[PROBE] A-END (grabbed)";

        // [PROBE] C: size sweep - find the canvas size where rendering fails.
        for (const QSize& sz : {QSize(1200, 900), QSize(900, 700),
                                QSize(700, 600), QSize(500, 400),
                                QSize(300, 250)}) {
            win.resize(sz);
            qDebug().noquote() << "[PROBE] C: window" << sz;
            pumpFor(700);
        }
    }

    pumpFor(300);

    selScenario:
    // ------------------------------------------------------------------
    // Scenario D: GPU + text selection -> click on page text.
    // Window is intentionally leaked (never destroyed) so any crash here is
    // from the click path itself, not teardown.
    // ------------------------------------------------------------------
    {
        // In sel-mode the window is leaked to isolate teardown from click
        // crashes; in normal flow it lives on the stack.
        QMainWindow winObj;
        QMainWindow* win = onlySelection ? new QMainWindow : &winObj;
        win->resize(1024, 768);
        win->show();

        PdfDocument* doc = new PdfDocument(win);
        SkiaPdfViewerWidget* gpu = new SkiaPdfViewerWidget(win);
        gpu->setDocument(doc);
        win->setCentralWidget(gpu);
        gpu->show();
        gpu->raise();
        pumpFor(400);

        bool ok = doc->load(pdfPath, QString());
        qDebug().noquote() << "[PROBE] D: load ok=" << ok;
        // Click immediately while async renders are in flight.
        pumpFor(20);

        gpu->setTextSelectionEnabled(true);
        qDebug().noquote() << "[PROBE] D: selection on, pageRect="
                           << gpu->pageRect().toRect();

        // Click on a known text position WITHOUT pre-building charMap so the
        // first click triggers the lazy charMap build on the GUI thread while
        // background renders may still be in flight (a race the user hit).
        {
            const QRectF pr = gpu->pageRect();
            const QPointF dev = doc->pageToDevice(0, QPointF(80, 744),
                                                  QPoint(0, 0),
                                                  pr.size().toSize(), 0);
            qDebug().noquote() << "[PROBE] D: first click pos (lazy build)="
                               << pr.topLeft() + dev;
        }

        const QPoint base = [&]() {
            const QRectF pr = gpu->pageRect();
            const QPointF dev = doc->pageToDevice(0, QPointF(80, 744),
                                                  QPoint(0, 0),
                                                  pr.size().toSize(), 0);
            return (pr.topLeft() + dev).toPoint();
        }();
        const QPoint grid[] = { base,
                                base + QPoint(4, -4),
                                base + QPoint(-6, 6),
                                base + QPoint(40, 10),
                                gpu->pageRect().center().toPoint(),
                                base + QPoint(-120, -40) };
        for (int i = 0; i < 6; ++i) {
            qDebug().noquote() << "[PROBE] D: click" << i << "at" << grid[i];
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(grid[i]),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(grid[i]),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &press);
            QApplication::sendEvent(gpu, &release);
            pumpFor(40);
        }

        // Double-click (word selection path: wordRange).
        qDebug().noquote() << "[PROBE] D: dblclick at" << base;
        {
            QMouseEvent p1(QEvent::MouseButtonPress, QPointF(base),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent r1(QEvent::MouseButtonRelease, QPointF(base),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &p1);
            QApplication::sendEvent(gpu, &r1);
            QMouseEvent p2(QEvent::MouseButtonDblClick, QPointF(base),
                           Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent r2(QEvent::MouseButtonRelease, QPointF(base),
                           Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &p2);
            QApplication::sendEvent(gpu, &r2);
            pumpFor(150);
        }

        // Drag-select: press at base, move across several lines, release.
        qDebug().noquote() << "[PROBE] D: drag from" << base << "by"
                           << QPoint(0, 300);
        {
            QMouseEvent p(QEvent::MouseButtonPress, QPointF(base),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &p);
            const QPoint end = base + QPoint(0, 300);
            for (int step = 1; step <= 5; ++step) {
                QPointF pos = QPointF(base) + (QPointF(end) - QPointF(base)) *
                                              (step / 5.0);
                QMouseEvent m(QEvent::MouseMove, pos, Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(gpu, &m);
                pumpFor(15);
            }
            QMouseEvent r(QEvent::MouseButtonRelease, QPointF(end),
                          Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(gpu, &r);
            pumpFor(150);
        }
        pumpFor(500);

        QImage fb = gpu->grabFramebuffer();
        report("D framebuffer after click", fb);
        qDebug().noquote() << "[PROBE] D-END (clicked, alive)";
    }

    qDebug().noquote() << "[PROBE] done";
    return 0;
}