#ifndef FPDF_EXT_SKIA_H_
#define FPDF_EXT_SKIA_H_

#include "public/fpdfview.h"
#include "third_party/skia/include/core/SkCanvas.h"

// Renders a PDF page directly onto a Skia canvas.
// Returns true on success, false otherwise.
bool FPDF_RenderPageToCanvas(FPDF_PAGE page,
                             int start_x,
                             int start_y,
                             int size_x,
                             int size_y,
                             int rotation,
                             int flags,
                             SkCanvas* canvas);

#endif // FPDF_EXT_SKIA_H_
