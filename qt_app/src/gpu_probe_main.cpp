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
        gpu->setTextSelectionEnabled(false);
        win.setCentralWidget(gpu);
        gpu->show();
        gpu->raise();
        pumpFor(5000);

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

    // ------------------------------------------------------------------
    // Scenario B: "toggle first, then open" (a fresh GPU-only window, doc is
    // loaded through the shared PdfDocument -> loadFinished syncing).
    // ------------------------------------------------------------------
    {
        QMainWindow win;
        win.resize(1024, 768);
        win.show();

        PdfDocument* doc = new PdfDocument(&win);
        SkiaPdfViewerWidget* gpu = new SkiaPdfViewerWidget(&win);
        gpu->setDocument(doc);
        win.setCentralWidget(gpu);
        gpu->show();
        gpu->raise();
        pumpFor(400);

        qDebug().noquote() << "[PROBE] B: load via PdfDocument::load";
        bool ok = doc->load(pdfPath, QString());
        qDebug().noquote() << "[PROBE] B: load ok=" << ok
                           << "gpu page/zoom=" << gpu->currentPage() << gpu->zoom();
        pumpFor(2500);

        QImage fb = gpu->grabFramebuffer();
        report("B framebuffer", fb);
    }

    qDebug().noquote() << "[PROBE] done";
    return 0;
}