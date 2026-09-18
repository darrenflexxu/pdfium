#ifndef FPDF_EXT_SKIA_H_
#define FPDF_EXT_SKIA_H_

#include "public/fpdfview.h"
#include "third_party/skia/include/core/SkCanvas.h"

// Renders a PDF page directly onto a Skia canvas.
// `rotation` is in degrees (0, 90, 180, 270); it is normalized to the
// quarter-turn value (0..3) that CPDFSDK_RenderPageWithContext expects,
// mirroring how the wrapper's bitmap Render() converts degrees.
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
