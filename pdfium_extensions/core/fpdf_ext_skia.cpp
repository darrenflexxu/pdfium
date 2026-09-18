#include "fpdf_ext_skia.h"

#include "core/fpdfapi/page/cpdf_page.h"
#include "core/fpdfapi/render/cpdf_pagerendercontext.h"
#include "core/fxge/cfx_renderdevice.h"
#include "fpdfsdk/cpdfsdk_helpers.h"
#include "fpdfsdk/cpdfsdk_renderpage.h"
#include "public/cpp/fpdf_scopers.h"

bool FPDF_RenderPageToCanvas(FPDF_PAGE page,
                             int start_x,
                             int start_y,
                             int size_x,
                             int size_y,
                             int rotation,
                             int flags,
                             SkCanvas* canvas) {
    if (!page || !canvas) return false;

    CPDF_Page* cpdf_page = CPDFPageFromFPDFPage(page);
    if (!cpdf_page) return false;

    // Create a render context for the page.
    auto context = std::make_unique<CPDF_PageRenderContext>();
    CPDF_PageRenderContext* unowned_context = context.get();

    // Use a clearer to ensure the page's render context is restored after this call.
    CPDF_Page::RenderContextClearer clearer(cpdf_page);
    cpdf_page->SetRenderContext(std::move(context));

    // Create a Skia-backed render device.
    auto default_device = CFX_RenderDevice::CreateForSkiaCanvas(*canvas);
    if (!default_device) return false;
    unowned_context->device_ = std::move(default_device);

    // This extension's public API takes rotation in DEGREES (0/90/180/270),
    // matching how the widget stores it. CPDFSDK_RenderPageWithContext (like
    // FPDF_RenderPageBitmap) expects QUARTER-TURNS (0..3); GetDisplayMatrixForRect
    // does `rotation % 4` on it, so passing raw degrees would map 90 and 270 to
    // 180 and leave 180 unrotated. Normalize before handing off.
    const int pdfium_rotation = ((rotation / 90) % 4 + 4) % 4;

    // Execute the rendering.
    // CPDFSDK_RenderPageWithContext is the internal entry point for rendering a page.
    CPDFSDK_RenderPageWithContext(unowned_context, cpdf_page, start_x, start_y,
                                  size_x, size_y, pdfium_rotation, flags,
                                  /*color_scheme=*/nullptr,
                                  /*need_to_restore=*/true, /*pause=*/nullptr);

    return true;
}
