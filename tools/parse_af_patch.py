"""Decode an Alpha Forever designer-clipboard patch export (base64 .txt,
as served from the afmodular.com patch pages) into a readable listing:
components, knob/const values, and wiring.

Formats seen:
- swdesignerclipboard::20150715 (AFNoding-031 acoustic piano): int32
  positions; TOP-level components carry a name-line trailer; nested
  swdesigner::2018xxxx components do not.
- swdesignerclipboard::20200430 (AF_KS): extra patch-name line after the
  header; float32 positions; swsubdesigner::20200717 payload = version
  line + flag byte + length line + JSON blob + 8B view + inner designer.

Usage: python tools/parse_af_patch.py <in.txt> [out.txt]
"""
import base64, struct, sys, re

# type -> serialized payload size in bytes (name trailer handled separately)
PAYLOAD = {
    'swmidinote': 0, 'swgain': 8, 'swout': 0, 'swnoise': 0,
    'swenvfollowmod': 16, 'swsuperknob': 28, 'swtrgtogate': 8,
    'swfilter1p': 0, 'swfilter2p': 0, 'swcrossfade': 0, 'swin': 0,
    'swconst': 4, 'swmidicc': 4, 'swsamplerate': 0, 'swdelay': 0,
    'swallpass': 0,
}
COMP_RE = re.compile(rb'sw[a-z0-9]+\n')


def read_line(b, pos):
    end = b.index(b'\n', pos)
    return b[pos:end].decode('latin-1'), end + 1


def fmt_state(state):
    vals = []
    for i in range(0, len(state) // 4 * 4, 4):
        fv = struct.unpack_from('<f', state, i)[0]
        iv = struct.unpack_from('<i', state, i)[0]
        vals.append(f'{fv:g}' if (fv == 0 or 1e-6 < abs(fv) < 1e7) else f'i{iv}')
    return ' '.join(vals)


def parse_designer(b, pos, depth, out):
    n = struct.unpack_from('<i', b, pos)[0]
    pos += 4
    ind = '  ' * depth
    for k in range(n):
        ctype, pos = read_line(b, pos)
        _sw, pos = read_line(b, pos)
        name, pos = read_line(b, pos)
        pos += 8  # position (int32 pair in 2015, float pair in 2019+)
        header = ctype.encode('latin-1') + b'::'
        state = b''
        if b[pos:pos + len(header)] == header:
            hdr, pos = read_line(b, pos)
            if ctype == 'swsubdesigner':
                if hdr.endswith('20200717'):
                    _ver, pos = read_line(b, pos)       # "1"
                    pos += 1                            # flag byte
                    ln, pos = read_line(b, pos)         # JSON length
                    meta = b[pos:pos + int(ln)]
                    pos += int(ln)
                    pos += 8                            # view floats
                    out.append(f'{ind}[{k}] SUBPATCH "{name}"  meta={meta.decode("latin-1", "replace")!r}')
                else:
                    out.append(f'{ind}[{k}] SUBPATCH "{name}"')
                _inner, pos = read_line(b, pos)         # swdesigner::date
                pos = parse_designer(b, pos, depth + 1, out)
                pos += 12                               # view state
                tr = name.encode('latin-1') + b'\n'
                if b[pos:pos + len(tr)] == tr:
                    pos += len(tr)
                continue
            s = PAYLOAD.get(ctype)
            if s is None:
                # unknown type: scan for the next component boundary
                m = COMP_RE.search(b, pos)
                s = (m.start() if m else len(b)) - pos
                out.append(f'{ind}[{k}] {ctype:16s} "{name}" UNKNOWN-PAYLOAD[{s}B] {fmt_state(b[pos:pos+s])}')
                pos += s
                tr = name.encode('latin-1') + b'\n'
                if b[pos - len(tr):pos] == tr:
                    pass
                continue
            state = b[pos:pos + s]
            pos += s
            tr = name.encode('latin-1') + b'\n'
            if b[pos:pos + len(tr)] == tr:
                pos += len(tr)
        else:
            tr = name.encode('latin-1') + b'\n'
            if b[pos:pos + len(tr)] == tr:
                pos += len(tr)
        out.append(f'{ind}[{k}] {ctype:16s} "{name}" {fmt_state(state)}')
    # wiring: flagged text records
    while True:
        flag = struct.unpack_from('<i', b, pos)[0]
        pos += 4
        if flag == 0:
            break
        dstC, pos = read_line(b, pos)
        dstP, pos = read_line(b, pos)
        srcC, pos = read_line(b, pos)
        srcP, pos = read_line(b, pos)
        out.append(f'{ind}  {dstC}.{dstP} <- {srcC}.{srcP}')
    return pos


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    raw = open(sys.argv[1], 'rb').read()
    blob = base64.b64decode(b''.join(raw.split()))
    out = []
    hdr, pos = read_line(blob, 0)
    out.append(f'# {hdr}  ({len(blob)} bytes)')
    if not hdr.endswith(('20150715',)):
        pname, pos = read_line(blob, pos)   # 2020 clipboard: patch name line
        out.append(f'# patch name: {pname}')
    try:
        pos = parse_designer(blob, pos, 0, out)
        out.append(f'== done, leftover {len(blob) - pos} bytes')
        if len(blob) - pos:
            out.append(f'   tail: {blob[pos:pos+64].hex()}')
    except Exception as e:
        out.append(f'!! stopped: {e!r}')
    text = '\n'.join(out)
    if len(sys.argv) > 2:
        open(sys.argv[2], 'w', encoding='utf-8').write(text)
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
