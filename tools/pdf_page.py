#!/usr/bin/env python3
"""Convert one PDF page to a PNG.

Usage: pdf_page.py <input.pdf> <page> <output.png> [dpi]
Page is 1-indexed.
"""
import sys
import fitz  # PyMuPDF

def main():
    if len(sys.argv) < 4:
        print("usage: pdf_page.py <input.pdf> <page> <output.png> [dpi]", file=sys.stderr)
        sys.exit(1)
    pdf_path = sys.argv[1]
    page = int(sys.argv[2]) - 1
    out_path = sys.argv[3]
    dpi = int(sys.argv[4]) if len(sys.argv) >= 5 else 300

    doc = fitz.open(pdf_path)
    if page < 0 or page >= len(doc):
        print(f"page out of range: {page+1} of {len(doc)}", file=sys.stderr)
        sys.exit(1)
    pix = doc[page].get_pixmap(dpi=dpi)
    pix.save(out_path)
    print(f"ok: {pdf_path} page {page+1} -> {out_path} ({pix.width}x{pix.height} @ {dpi}dpi)")

if __name__ == "__main__":
    main()
