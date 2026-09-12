#include "pdfdocument.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QEventLoop>
#include <QImage>
#include <iostream>
#include <vector>
#include <cmath>

#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"

static constexpr int kW = 300;
static constexpr int kH = 200;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: skia_gl_harness <pdf_path>\n";
        return 1;
    }

    QGuiApplication app(argc, argv);

    // Offscreen GL context (no windowing session needed).
    QOffscreenSurface offscreen;
    offscreen.create();
    QOpenGLContext glCtx;
    if (!glCtx.create()) {
        std::cerr << "HARNESS FATAL: QOpenGLContext::create() failed\n";
        return 1;
    }
    glCtx.makeCurrent(&offscreen);

    // Skia Ganesh GL context.
    sk_sp<const GrGLInterface> glIface = GrGLMakeNativeInterface();
    if (!glIface) {
        std::cerr << "HARNESS FATAL: GrGLMakeNativeInterface() returned nullptr\n";
        return 1;
    }
    sk_sp<GrDirectContext> grCtx = GrDirectContexts::MakeGL(glIface);
    if (!grCtx) {
        std::cerr << "HARNESS FATAL: GrDirectContexts::MakeGL() returned nullptr\n";
        return 1;
    }

    // Load the PDF and render page 0 at a known size (CPU raster reference).
    PdfDocument doc;
    if (!doc.load(QString::fromLocal8Bit(argv[1]))) {
        std::cerr << "HARNESS FATAL: PdfDocument::load() failed\n";
        return 1;
    }
    if (doc.pageCount() <= 0) {
        std::cerr << "HARNESS FATAL: document has no pages\n";
        return 1;
    }

    QImage cpuImg;
    QEventLoop loop;
    QObject::connect(&doc, &PdfDocument::renderFinished,
                     [&](int, QImage img, bool ok) { if (ok) cpuImg = img; loop.quit(); });
    doc.requestRender(0, QSize(kW, kH), 0);
    loop.exec();
    if (cpuImg.isNull()) {
        std::cerr << "HARNESS FATAL: CPU render failed\n";
        return 1;
    }

    // GPU surface and upload: mirrors the widget's paintGL() drawing path.
    const SkImageInfo gpuInfo = SkImageInfo::Make(
        kW, kH, kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> gpuSurface = SkSurfaces::RenderTarget(
        grCtx.get(), skgpu::Budgeted::kYes, gpuInfo, 0,
        kTopLeft_GrSurfaceOrigin, nullptr);
    if (!gpuSurface) {
        std::cerr << "HARNESS FATAL: SkSurfaces::RenderTarget() returned nullptr\n";
        return 1;
    }
    SkCanvas* canvas = gpuSurface->getCanvas();
    canvas->clear(SK_ColorWHITE);

    const SkImageInfo cpuInfo = SkImageInfo::Make(
        cpuImg.width(), cpuImg.height(), kBGRA_8888_SkColorType, kPremul_SkAlphaType);
    SkPixmap pm(cpuInfo, cpuImg.constBits(), cpuImg.bytesPerLine());
    sk_sp<SkImage> skImg = SkImages::RasterFromPixmapCopy(pm);
    if (skImg) {
        canvas->drawImageRect(
            skImg.get(),
            SkRect::MakeIWH(cpuImg.width(), cpuImg.height()),
            SkRect::MakeIWH(kW, kH),
            SkSamplingOptions(SkFilterMode::kLinear), nullptr,
            SkCanvas::kFast_SrcRectConstraint);
    }
    grCtx->flushAndSubmit();

    // Read back in RGBA (the GPU surface's own layout).
    const SkImageInfo readInfo = SkImageInfo::Make(
        kW, kH, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const size_t rowBytes = kW * 4;
    std::vector<uint8_t> gpuBuf(rowBytes * kH);
    if (!gpuSurface->readPixels(readInfo, gpuBuf.data(), rowBytes, 0, 0)) {
        std::cerr << "HARNESS FATAL: readPixels() failed\n";
        return 1;
    }

    // Compare: cpuImg is ARGB32_Premultiplied = B,G,R,A bytes on little-endian;
    // the GPU readback is kRGBA_8888 = R,G,B,A bytes.
    int mismatches = 0;
    for (int y = 0; y < kH; ++y) {
        const uint8_t* cpuLine = cpuImg.constScanLine(y);
        const uint8_t* gpuLine = &gpuBuf[y * rowBytes];
        for (int x = 0; x < kW; ++x) {
            const bool ok =
                std::abs(int(cpuLine[2]) - int(gpuLine[0])) <= 4 &&
                std::abs(int(cpuLine[1]) - int(gpuLine[1])) <= 4 &&
                std::abs(int(cpuLine[0]) - int(gpuLine[2])) <= 4 &&
                std::abs(int(cpuLine[3]) - int(gpuLine[3])) <= 4;
            if (!ok) ++mismatches;
            cpuLine += 4;
            gpuLine += 4;
        }
    }

    const int total = kW * kH;
    const bool pass = mismatches <= total / 500;  // <=0.2% (premul rounding slack)
    std::cout << "HARNESS " << (pass ? "PASS" : "FAIL")
              << "  mismatches=" << mismatches << "/" << total
              << "  (slack " << total / 500 << ")\n";
    glCtx.doneCurrent();
    return pass ? 0 : 1;
}