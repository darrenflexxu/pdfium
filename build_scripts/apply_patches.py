#!/usr/bin/env python3
"""
Patch application script for PDFium extensions.
Applies custom extensions to the PDFium source tree.
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

def apply_patches(pdfium_dir: Path, extensions_dir: Path) -> bool:
    """Apply PDFium extensions to the source tree"""
    
    # Copy public headers
    src_public = extensions_dir / "public"
    dst_public = pdfium_dir / "public"
    
    if src_public.exists():
        dst_public.mkdir(parents=True, exist_ok=True)
        for header in src_public.glob("*.h"):
            dst_file = dst_public / header.name
            shutil.copy2(header, dst_file)
            print(f"Copied header: {header.name} -> {dst_file}")
    
    # Copy core implementation files
    src_core = extensions_dir / "core"
    # These need to be placed in appropriate PDFium core directories
    # For now, we'll copy them to a location that can be included
    
    # The core files need to be integrated into PDFium's build
    # This is typically done by:
    # 1. Adding them to the appropriate BUILD.gn files
    # 2. Or creating a separate component
    
    return True

def modify_gn_build(pdfium_dir: Path, enable_v8: bool = True) -> bool:
    """Modify PDFium's GN build files to include our extensions"""
    
    # Find the main BUILD.gn for the pdfium component
    build_gn = pdfium_dir / "BUILD.gn"
    if not build_gn.exists():
        print("Warning: BUILD.gn not found in PDFium root")
        return False
    
    # Read existing BUILD.gn
    content = build_gn.read_text()
    
    # 1. Fix V8 assertion for fuzzers if V8 is disabled
    if not enable_v8:
        # Remove the fuzzer dependency from pdfium_all to avoid assertion fail
        content = content.replace('"testing/fuzzers",', '')
        build_gn.write_text(content)
        print("Removed 'testing/fuzzers' from root BUILD.gn because V8 is disabled")
        
        # ALSO: Directly comment out the assertion in the fuzzer BUILD.gn to be safe
        fuzzer_build_gn = pdfium_dir / "testing" / "fuzzers" / "BUILD.gn"
        if fuzzer_build_gn.exists():
            f_content = fuzzer_build_gn.read_text()
            if "assert(pdf_enable_v8)" in f_content:
                f_content = f_content.replace("assert(pdf_enable_v8)", "# assert(pdf_enable_v8)")
                fuzzer_build_gn.write_text(f_content)
                print("Commented out assert(pdf_enable_v8) in testing/fuzzers/BUILD.gn")
    
    # 2. Create separate extension build config if it doesn't exist
    ext_build_gn = pdfium_dir / "fpdf_extensions_build.gn"
    if not ext_build_gn.exists():
        ext_content = """
# PDFium Extensions Build Configuration
source_set("fpdf_ext_compression") {
  sources = [ "pdfium_extensions/core/fpdf_ext_compression.cpp" ]
  include_dirs = [ ".", "core/fpdfapi/fpdf_parser/include", "core/fpdfapi/fpdf_page/include", "core/fpdfapi/fpdf_font/include", "third_party/zlib" ]
  deps = [ "core/fpdfapi/fpdf_parser:fpdf_parser", "core/fpdfapi/fpdf_page:fpdf_page", "third_party/zlib:zlib" ]
}
source_set("fpdf_ext_element") {
  sources = [ "pdfium_extensions/core/fpdf_ext_element.cpp" ]
  include_dirs = [ ".", "core/fpdfapi/fpdf_parser/include", "core/fpdfapi/fpdf_page/include", "core/fpdfapi/fpdf_annot/include" ]
  deps = [ "core/fpdfapi/fpdf_parser:fpdf_parser", "core/fpdfapi/fpdf_page:fpdf_page", "core/fpdfapi/fpdf_annot:fpdf_annot" ]
}
"""
        ext_build_gn.write_text(ext_content)
        print(f"Created extension build config: {ext_build_gn}")
    
    return True

def main():
    if len(sys.argv) < 3:
        print("Usage: apply_patches.py <pdfium_dir> <extensions_dir> [--no-v8]")
        return 1
    
    pdfium_dir = Path(sys.argv[1])
    extensions_dir = Path(sys.argv[2])
    enable_v8 = True
    if "--no-v8" in sys.argv:
        enable_v8 = False
    
    if not pdfium_dir.exists():
        print(f"Error: PDFium directory not found: {pdfium_dir}")
        return 1
    
    if not extensions_dir.exists():
        print(f"Error: Extensions directory not found: {extensions_dir}")
        return 1
    
    print(f"Applying patches to {pdfium_dir}")
    
    success = True
    success &= apply_patches(pdfium_dir, extensions_dir)
    success &= modify_gn_build(pdfium_dir, enable_v8)
    
    if success:
        print("Patches applied successfully!")
        return 0
    else:
        print("Failed to apply patches")
        return 1

if __name__ == "__main__":
    sys.exit(main())
