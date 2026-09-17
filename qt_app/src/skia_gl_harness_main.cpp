#include "pdfdocument.h"

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QEventLoop>
#include <QImage>
#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: skia_gl_harness <pdf_path> [width height]\n";
        return 1;
    }
    const int kW = argc > 2 ? std::atoi(argv[2]) : 300;
    const int kH = argc > 3 ? std::atoi(argv[3]) : 200;

    QGuiApplication app(argc, argv);

    // Request a 3.3 core-profile context: Skia's Ganesh shaders use GLSL
    // 1.30+ features (gl_VertexID) that a legacy 2.1 context cannot compile.
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    // Offscreen GL context (no windowing session needed).
    QOffscreenSurface offscreen;
    offscreen.setFormat(fmt);
    offscreen.create();
    QOpenGLContext glCtx;
    glCtx.setFormat(fmt);
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

    // Stage 2: mirror SkiaPdfViewerWidget::paintGL(), which wraps the widget's
    // default FBO with GrBackendRenderTargets::MakeGL + WrapBackendRenderTarget
    // (top-left origin, kRGBA_8888) and draws the cached PDFium page into it.
    // Qt's offscreen platform default framebuffer has no usable color buffer for
    // glReadPixels (GL_INVALID_OPERATION), so allocate a real RGBA8 FBO first.
    {
        QOpenGLFunctions* glf = QOpenGLContext::currentContext()->functions();
        GLuint fbo = 0, rbo = 0;
        glf->glGenFramebuffers(1, &fbo);
        glf->glGenRenderbuffers(1, &rbo);
        glf->glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glf->glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, kW, kH);
        glf->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glf->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_RENDERBUFFER, rbo);
        if (glf->glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "HARNESS STAGE2 FATAL: FBO incomplete\n";
            return 1;
        }

        GrGLFramebufferInfo fbInfo = {};
        fbInfo.fFBOID = fbo;
        fbInfo.fFormat = 0x8058;        // GL_RGBA8
        GrBackendRenderTarget backend =
            GrBackendRenderTargets::MakeGL(kW, kH, 0, 8, fbInfo);

        grCtx->flushAndSubmit();

        // Stage 2 renders DIRECTLY onto a GL-backed SkCanvas through the
        // wrapper's IPdfPage::RenderToCanvas (plan §12 step 1:
        // FPDF_RenderPageToCanvas must emit drawing into a canvas). This
        // mirrors SkiaPdfViewerWidget::paintGL(), where PDFium's Skia device
        // records straight into the widget's framebuffer.
        sk_sp<SkSurface> wrapped = SkSurfaces::WrapBackendRenderTarget(
            grCtx.get(), backend, kTopLeft_GrSurfaceOrigin,
            kRGBA_8888_SkColorType, nullptr, nullptr);
        if (!wrapped) {
            std::cerr << "HARNESS STAGE2 FATAL: WrapBackendRenderTarget() failed\n";
            return 1;
        }
        SkCanvas* wcanvas = wrapped->getCanvas();
        wcanvas->clear(SK_ColorWHITE);

        IPdfPage* page = doc.interface()->GetPage(0);
        if (!page) {
            std::cerr << "HARNESS STAGE2 FATAL: GetPage(0) returned nullptr\n";
            return 1;
        }
        const bool rendered =
            page->RenderToCanvas(wcanvas, 0, 0, kW, kH, 0, PDF_RENDER_ANNOTATIONS);
        page->Release();
        grCtx->flushAndSubmit();
        if (!rendered) {
            std::cerr << "HARNESS STAGE2 FATAL: RenderToCanvas() failed\n";
            return 1;
        }

        const SkImageInfo readInfo = SkImageInfo::Make(
            kW, kH, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        std::vector<uint8_t> directBuf(rowBytes * kH);
        if (!wrapped->readPixels(readInfo, directBuf.data(), rowBytes, 0, 0)) {
            std::cerr << "HARNESS STAGE2 FATAL: readPixels() failed\n";
            return 1;
        }

        // The canvas renderer (Skia) and the bitmap renderer differ slightly at
        // antialiased edges, so use a loose tolerance and also require that the
        // direct output actually contains ink at a coverage comparable to the
        // CPU reference (i.e. the page was really drawn, not left blank).
        int mismatch = 0, directInk = 0, refInk = 0;
        for (int y = 0; y < kH; ++y) {
            const uint8_t* cpuLine = cpuImg.constScanLine(y);
            const uint8_t* gpuLine = &directBuf[y * rowBytes];
            for (int x = 0; x < kW; ++x) {
                const bool dif =
                    std::abs(int(cpuLine[2]) - int(gpuLine[0])) > 48 ||
                    std::abs(int(cpuLine[1]) - int(gpuLine[1])) > 48 ||
                    std::abs(int(cpuLine[0]) - int(gpuLine[2])) > 48;
                if (dif) ++mismatch;
                if (gpuLine[0] < 250 || gpuLine[1] < 250 || gpuLine[2] < 250) ++directInk;
                if (cpuLine[0] < 250 || cpuLine[1] < 250 || cpuLine[2] < 250) ++refInk;
                cpuLine += 4;
                gpuLine += 4;
            }
        }
        const bool contentOk = directInk > 0 && refInk > 0 &&
                               directInk >= refInk / 4 && directInk <= refInk * 4;
        const bool directPass = contentOk && mismatch <= total / 8;
        std::cout << "HARNESS STAGE2 " << (directPass ? "PASS" : "FAIL")
                  << "  directInk=" << directInk << " refInk=" << refInk
                  << " mismatch(>48)=" << mismatch << "/" << total << "\n";

        // Diagnostic: per-band ink coverage (detects whole regions that failed
        // to render). Also dump both images for offline inspection.
        {
            cpuImg.save(QStringLiteral("/tmp/harness_ref.png"));
            QImage directImg(reinterpret_cast<const uchar*>(directBuf.data()),
                             kW, kH, rowBytes, QImage::Format_RGBA8888);
            directImg.save(QStringLiteral("/tmp/harness_direct.png"));

            int rx0 = kW, ry0 = kH, rx1 = -1, ry1 = -1;
            int dx0 = kW, dy0 = kH, dx1 = -1, dy1 = -1;
            for (int y = 0; y < kH; ++y) {
                const uint8_t* cpuLine = cpuImg.constScanLine(y);
                const uint8_t* gpuLine = &directBuf[static_cast<size_t>(y) * rowBytes];
                for (int x = 0; x < kW; ++x) {
                    if (cpuLine[0] < 250 || cpuLine[1] < 250 || cpuLine[2] < 250) {
                        rx0 = std::min(rx0, x); rx1 = std::max(rx1, x);
                        ry0 = std::min(ry0, y); ry1 = std::max(ry1, y);
                    }
                    if (gpuLine[0] < 250 || gpuLine[1] < 250 || gpuLine[2] < 250) {
                        dx0 = std::min(dx0, x); dx1 = std::max(dx1, x);
                        dy0 = std::min(dy0, y); dy1 = std::max(dy1, y);
                    }
                    cpuLine += 4;
                    gpuLine += 4;
                }
            }
            std::cout << "  bbox ref=[" << rx0 << "," << ry0 << " " << rx1 << ","
                      << ry1 << "] direct=[" << dx0 << "," << dy0 << " " << dx1
                      << "," << dy1 << "]\n";

            const char* names[3] = {"top", "mid", "bot"};
            for (int band = 0; band < 3; ++band) {
                const int y0 = kH * band / 3;
                const int y1 = kH * (band + 1) / 3;
                int r = 0, d = 0;
                for (int y = y0; y < y1; ++y) {
                    const uint8_t* cpuLine = cpuImg.constScanLine(y);
                    const uint8_t* gpuLine = &directBuf[static_cast<size_t>(y) * rowBytes];
                    for (int x = 0; x < kW; ++x) {
                        if (cpuLine[0] < 250 || cpuLine[1] < 250 || cpuLine[2] < 250) ++r;
                        if (gpuLine[0] < 250 || gpuLine[1] < 250 || gpuLine[2] < 250) ++d;
                        cpuLine += 4;
                        gpuLine += 4;
                    }
                }
                std::cout << "  band " << names[band]
                          << " ref=" << r << " direct=" << d << "\n";
            }
        }

        if (!directPass) {
            std::cout << "  (images: /tmp/harness_ref.png /tmp/harness_direct.png)\n";
            return 1;
        }

        // Diagnostic: does PDFium paint the page's default white background
        // onto an external canvas, or leave the existing pixels? (The bitmap
        // path starts from a white bitmap; the widget clears to dark grey.)
        {
            SkCanvas* c = wrapped->getCanvas();
            c->clear(SkColorSetARGB(255, 128, 128, 128));
            IPdfPage* fp = doc.interface()->GetPage(0);
            if (fp) {
                fp->RenderToCanvas(c, 0, 0, kW, kH, 0, PDF_RENDER_ANNOTATIONS);
                fp->Release();
            }
            grCtx->flushAndSubmit();
            std::vector<uint8_t> bgBuf(rowBytes * kH);
            if (wrapped->readPixels(readInfo, bgBuf.data(), rowBytes, 0, 0)) {
                int white = 0, gray = 0, other = 0;
                for (int y = 0; y < kH; ++y) {
                    const uint8_t* p = &bgBuf[static_cast<size_t>(y) * rowBytes];
                    for (int x = 0; x < kW; ++x) {
                        const int r = p[0], g = p[1], b = p[2];
                        if (r > 250 && g > 250 && b > 250) ++white;
                        else if (std::abs(r - 128) < 8 && std::abs(g - 128) < 8 &&
                                 std::abs(b - 128) < 8) ++gray;
                        else ++other;
                        p += 4;
                    }
                }
                std::cout << "  bgcheck white=" << white << " gray=" << gray
                          << " other=" << other << "/" << total << "\n";
            }
        }

        // Stage 2C: replicate the widget's flipped canvas transform
        // (translate(0,h) + scale(dpr,-dpr)) to check whether the y-flip
        // interacts with PDFium's device clip/display matrix and drops content.
        {
            SkCanvas* c = wrapped->getCanvas();
            c->clear(SK_ColorWHITE);
            c->save();
            c->translate(0, kH);
            c->scale(1, -1);
            IPdfPage* fp = doc.interface()->GetPage(0);
            if (fp) {
                fp->RenderToCanvas(c, 0, 0, kW, kH, 0, PDF_RENDER_ANNOTATIONS);
                fp->Release();
            }
            c->restore();
            grCtx->flushAndSubmit();

            std::vector<uint8_t> flipBuf(rowBytes * kH);
            if (wrapped->readPixels(readInfo, flipBuf.data(), rowBytes, 0, 0)) {
                int fdiff = 0, fink = 0;
                for (int y = 0; y < kH; ++y) {
                    const uint8_t* a =
                        &directBuf[static_cast<size_t>(kH - 1 - y) * rowBytes];
                    const uint8_t* b = &flipBuf[static_cast<size_t>(y) * rowBytes];
                    for (int x = 0; x < kW; ++x) {
                        if (std::abs(int(a[0]) - int(b[0])) > 12 ||
                            std::abs(int(a[1]) - int(b[1])) > 12 ||
                            std::abs(int(a[2]) - int(b[2])) > 12)
                            ++fdiff;
                        if (b[0] < 250 || b[1] < 250 || b[2] < 250) ++fink;
                        a += 4;
                        b += 4;
                    }
                }
                std::cout << "HARNESS STAGE2C flip diff=" << fdiff << "/" << total
                          << " ink=" << fink << " (unflipped ink=" << directInk
                          << ")\n";
            }
        }

        // Stage 4: zoom/cull regression. The widget draws a page larger than
        // the viewport by passing a negative start offset (page centered).
        // PDFium culls objects using a clip box derived from the canvas'
        // device clip bounds, so the visible region must match the same region
        // of a full-size render. Guards the "elements disappear when zooming"
        // bug caused by baking the offset into the canvas transform.
        {
            const int zW = kW * 2, zH = kH * 2;
            // Reference: CPU (AGG) bitmap render at the zoomed size. This
            // path is independent of the Skia external-canvas clip box, so it
            // is a reliable reference for what the visible region must contain.
            QImage cpuZoom;
            QEventLoop zloop;
            QMetaObject::Connection zc = QObject::connect(
                &doc, &PdfDocument::renderFinished,
                [&](int, QImage img, bool ok) {
                    if (ok) cpuZoom = img;
                    zloop.quit();
                });
            doc.requestRender(0, QSize(zW, zH), 0);
            zloop.exec();
            QObject::disconnect(zc);
            if (cpuZoom.isNull()) {
                std::cout << "HARNESS STAGE4 FATAL: CPU zoom render failed\n";
                return 1;
            }

            // Pick a viewport-sized window over the zoomed page that actually
            // contains content (from the reference's ink bbox), so the test is
            // meaningful for pages whose content is not centered. The window
            // origin is passed to PDFium as the layout rectangle offset.
            int bx0 = zW, by0 = zH, bx1 = -1, by1 = -1;
            for (int y = 0; y < cpuZoom.height(); ++y) {
                const uint8_t* p = cpuZoom.constScanLine(y);
                for (int x = 0; x < cpuZoom.width(); ++x) {
                    if (p[0] < 250 || p[1] < 250 || p[2] < 250) {
                        bx0 = std::min(bx0, x);
                        bx1 = std::max(bx1, x);
                        by0 = std::min(by0, y);
                        by1 = std::max(by1, y);
                    }
                    p += 4;
                }
            }
            int cropX = 0, cropY = 0;
            if (bx1 >= bx0) {
                cropX = std::max(0, std::min(zW - kW, (bx0 + bx1) / 2 - kW / 2));
                cropY = std::max(0, std::min(zH - kH, (by0 + by1) / 2 - kH / 2));
            }
            const int offX = -cropX, offY = -cropY;

            // Widget emulation: zoomed page centered on the viewport-sized
            // canvas, offset passed to PDFium (not baked into the transform).
            SkCanvas* c = wrapped->getCanvas();
            c->clear(SK_ColorWHITE);
            IPdfPage* wp = doc.interface()->GetPage(0);
            bool ok = wp && wp->RenderToCanvas(c, offX, offY, zW, zH, 0,
                                               PDF_RENDER_ANNOTATIONS);
            if (wp) wp->Release();
            grCtx->flushAndSubmit();
            std::vector<uint8_t> emuBuf(static_cast<size_t>(rowBytes) * kH, 0);
            ok = ok && wrapped->readPixels(readInfo, emuBuf.data(), rowBytes, 0, 0);
            if (ok) {
                int zrefInk = 0, emuInk = 0, zdiff = 0;
                for (int y = 0; y < kH; ++y) {
                    // cpuZoom is ARGB32_Premultiplied (B,G,R,A bytes);
                    // emuBuf is RGBA readback.
                    const uint8_t* rl =
                        cpuZoom.constScanLine(cropY + y) + cropX * 4;
                    const uint8_t* el = &emuBuf[static_cast<size_t>(y) * rowBytes];
                    for (int x = 0; x < kW; ++x) {
                        if (rl[0] < 250 || rl[1] < 250 || rl[2] < 250) ++zrefInk;
                        if (el[0] < 250 || el[1] < 250 || el[2] < 250) ++emuInk;
                        if (std::abs(int(rl[2]) - int(el[0])) > 48 ||
                            std::abs(int(rl[1]) - int(el[1])) > 48 ||
                            std::abs(int(rl[0]) - int(el[2])) > 48) ++zdiff;
                        rl += 4;
                        el += 4;
                    }
                }
                const bool inkOk =
                    zrefInk == 0
                        ? emuInk <= total / 200
                        : (emuInk >= zrefInk / 2 && emuInk <= zrefInk * 3 + 500);
                const bool zpass = inkOk && zdiff <= total / 8;
                std::cout << "HARNESS STAGE4 zoom " << (zpass ? "PASS" : "FAIL")
                          << " off=" << offX << "," << offY
                          << " refInk=" << zrefInk << " emuInk=" << emuInk
                          << " diff(>48)=" << zdiff << "/" << total << "\n";
                if (!zpass) {
                    cpuZoom.copy(cropX, cropY, kW, kH)
                        .save(QStringLiteral("/tmp/harness_zoom_ref.png"));
                    QImage emuImg(emuBuf.data(), kW, kH, rowBytes,
                                  QImage::Format_RGBA8888);
                    emuImg.save(QStringLiteral("/tmp/harness_zoom_emu.png"));
                    return 1;
                }
            } else {
                std::cout << "HARNESS STAGE4 FATAL\n";
                return 1;
            }

            // Sanity check: emulate the OLD (buggy) approach that bakes the
            // offset into the canvas transform, to confirm this test detects
            // the bug (large diff) rather than passing vacuously.
            {
                SkCanvas* oc = wrapped->getCanvas();
                oc->clear(SK_ColorWHITE);
                oc->save();
                oc->translate(offX, offY);
                IPdfPage* op = doc.interface()->GetPage(0);
                if (op) {
                    op->RenderToCanvas(oc, 0, 0, zW, zH, 0,
                                       PDF_RENDER_ANNOTATIONS);
                    op->Release();
                }
                oc->restore();
                grCtx->flushAndSubmit();
                std::vector<uint8_t> oldBuf(static_cast<size_t>(rowBytes) * kH, 0);
                if (wrapped->readPixels(readInfo, oldBuf.data(), rowBytes, 0, 0)) {
                    int oldDiff = 0, oldInk = 0;
                    for (int y = 0; y < kH; ++y) {
                        const uint8_t* rl =
                            cpuZoom.constScanLine(cropY + y) + cropX * 4;
                        const uint8_t* ol =
                            &oldBuf[static_cast<size_t>(y) * rowBytes];
                        for (int x = 0; x < kW; ++x) {
                            if (ol[0] < 250 || ol[1] < 250 || ol[2] < 250) ++oldInk;
                            if (std::abs(int(rl[2]) - int(ol[0])) > 48 ||
                                std::abs(int(rl[1]) - int(ol[1])) > 48 ||
                                std::abs(int(rl[0]) - int(ol[2])) > 48) ++oldDiff;
                            rl += 4;
                            ol += 4;
                        }
                    }
                    std::cout << "  STAGE4 old(baked translate) ink=" << oldInk
                              << " diff=" << oldDiff << "/" << total << "\n";
                }
            }
        }

        // Stage 3: micro-benchmark of the direct RenderToCanvas path (plan §12
        // step 4). Measures the per-frame cost of drawing the page straight
        // into the GL-backed canvas, excluding Qt/window event overhead.
        {
            const int kIters = 30;
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < kIters; ++i) {
                IPdfPage* bp = doc.interface()->GetPage(0);
                if (!bp) break;
                bp->RenderToCanvas(wrapped->getCanvas(), 0, 0, kW, kH, 0,
                                   PDF_RENDER_ANNOTATIONS);
                bp->Release();
            }
            grCtx->flushAndSubmit();
            const auto t1 = std::chrono::steady_clock::now();
            const double ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count() / kIters;
            std::cout << "HARNESS STAGE3 direct avg=" << ms << " ms/frame ("
                      << (ms > 0 ? 1000.0 / ms : 0.0) << " fps budget)\n";
        }

        glf->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glf->glDeleteFramebuffers(1, &fbo);
        glf->glDeleteRenderbuffers(1, &rbo);
    }

    glCtx.doneCurrent();
    return pass ? 0 : 1;
}