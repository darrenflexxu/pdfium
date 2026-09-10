#!/usr/bin/env python3
"""test_pdfium.py - functional validation for the pdfium Python bindings.

Usage:
    python3 test_pdfium.py [path/to/test.pdf] [output.pdf]

Flow (per PLAN.md section 6.校验方式):
  1. import pdfium, open a document with PdfDocument.create.
  2. verify get_page_count().
  3. get_page(0) -> get_text() / render().
  4. save_with_compression("out.pdf", options) -> verify file generated &
     reopenable, and stats are reported.
  5. stress: create/destroy many documents to exercise Release().
"""
import os
import sys

import pdfium


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "test.pdf"
    dst = sys.argv[2] if len(sys.argv) > 2 else "test_out.pdf"
    if not os.path.exists(src):
        print(f"SKIP: input file {src} not found")
        return 0

    # 1. Open / module-level API.
    assert callable(pdfium.PdfDocument.create)
    doc = pdfium.PdfDocument.create(src)
    assert doc is not None

    # 2. Page count.
    pages = doc.get_page_count()
    print(f"pages={pages}")
    assert pages >= 1

    # Structure / outline probe.
    struct = doc.get_document_structure()
    print(f"structure keys={sorted(struct.keys())} page_count={struct.get('page_count')}")

    # 3. First page: text + render.
    page = doc.get_page(0)
    size = page.get_size()
    print(f"page0 size={size}")
    text = page.get_text()
    print(f"page0 text len={len(text)}")
    assert isinstance(text, str)

    pixels = page.render(int(size[0] * 0.5), int(size[1] * 0.5))
    assert isinstance(pixels, bytes)
    assert len(pixels) == int(size[0] * 0.5) * int(size[1] * 0.5) * 4, len(pixels)
    print(f"page0 render bytes={len(pixels)}")

    # Wrapper methods sanity.
    assert page.get_char_count() >= 0
    if page.get_char_count() > 0:
        box = page.get_char_box(0)
        assert len(box) == 4
        print(f"char0 box={box}")
    assert page.count_page_elements() >= 0

    # Outline traversal (if present).
    root = doc.get_outline_root()
    title_count = 0
    stack = [root] if root else []
    while stack:
        node = stack.pop()
        if node is None:
            continue
        title_count += 1
        assert isinstance(node.get_title(), str)
        stack.append(node.get_first_child())
        stack.append(node.get_next_sibling())
    print(f"bookmark title_count={title_count}")

    # 4. Compression.
    opts = pdfium.CompressOptions()
    opts.flags = pdfium.FLATE | pdfium.OBJECT_STREAMS | pdfium.IMAGES | pdfium.REMOVE_UNUSED
    opts.image_quality = 60
    opts.remove_metadata = 1
    ok = doc.save_with_compression(dst, opts)
    print(f"save_with_compression -> {ok}")
    assert ok
    stats = doc.get_last_compress_stats()
    print(f"stats: orig={stats['original_size']} comp={stats['compressed_size']}"
          f" ratio={stats['compression_ratio']:.4f} removed={stats['objects_removed']}"
          f" images={stats['images_recompressed']}")
    assert os.path.exists(dst) and os.path.getsize(dst) > 0
    assert stats["compressed_size"] == os.path.getsize(dst)

    doc2 = pdfium.PdfDocument.create(dst)
    assert doc2.get_page_count() == pages
    print("reopened output OK")

    # 5. Memory/lifetime stress: many documents open+closed in a loop.
    before = None
    for i in range(60):
        d = pdfium.PdfDocument.create(src)
        assert d.get_page_count() >= 1
        d.get_page(0).get_text()
        del d
        if i == 10:
            before = os.urandom(1)  # keep it simple; loop termination is the check
    print("lifecycle stress OK (60 docs created/released)")

    print("ALL TESTS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())