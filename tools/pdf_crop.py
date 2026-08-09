#!/usr/bin/env python3
"""Convert one PDF page region to a PNG at high resolution.

Usage: pdf_crop.py <input.pdf> <page> <x0> <y0> <x1> <y1> <output.png> [dpi]
Coordinates are in PDF points (72/inch). Page is 1-indexed.
"""
import sys
import fitz

def main():
    if len(sys.argv) < 8:
        print("usage: pdf_crop.py <input.pdf> <page> <x0> <y0> <x1> <y1> <output.png> [dpi]",
              file=sys.stderr)
        sys.exit(1)
    pdf_path = sys.argv[1]
    page = int(sys.argv[2]) - 1
    x0, y0, x1, y1 = (float(sys.argv[i]) for i in range(3, 7))
    out_path = sys.argv[7]
    dpi = int(sys.argv[8]) if len(sys.argv) >= 9 else 600

    doc = fitz.open(pdf_path)
    p = doc[page]
    rect = fitz.Rect(x0, y0, x1, y1)
    pix = p.get_pixmap(dpi=dpi, clip=rect)
    pix.save(out_path)
    print(f"ok: page {page+1} clip {rect} -> {out_path} ({pix.width}x{pix.height} @ {dpi}dpi)")

    # Also print the page's full bounding box for reference.
    print(f"page mediabox: {p.mediabox}")

if __name__ == "__main__":
    main()
