#!/usr/bin/env python
"""Build a patched KeyStep Pro firmware update from YOUR OWN stock update file.

    python patches/make_patch.py generative filter-midicc
    python patches/make_patch.py filter-midicc --kspf "C:/path/to/keystep-pro_Firmware_Update_2.5.20.0.kspf" --out DIR
    python patches/make_patch.py filter-midicc --diff        # also print every changed byte (local debugging only)

Reads the stock .kspf (default: the MIDI Control Center install), refuses anything that is not byte-identical to
the firmware the patches were written for, applies the named patches to the main image, fixes the image header
(size, payload checksum, header checksum) and writes keystep-pro_Firmware_<version>+<patches>.kspf in the
current folder (or in --out). Flash that file with MIDI Control Center like any update; flash the stock file to go back.

The repo holds only our own bytes: each patch is a folder with its manifest (patch.json) and either one assembly
source (`"source"`, assembled with keystone) or C and assembly sources (`"sources"`, compiled and linked with the Arm
GNU toolchain at the patch's fixed place). Nothing of Arturia's firmware is stored here; the patcher reads it from
your machine.

Needs: keystone-engine (pip install keystone-engine); for C patches the Arm GNU Toolchain (arm-none-eabi-gcc;
ARM_GNU_BIN may point at its bin folder).
"""
import argparse, hashlib, json, os, re, struct, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_KSPF = r'C:\ProgramData\Arturia\MIDI Control Center\Firmware\keystep-pro_Firmware_Update_2.5.20.0.kspf'

# The stock images the patches are written for: SHA-256 of the image entry inside the .kspf.
STOCK = {
    'keysteppro_main__fw2_5_20_2085__2024_03_04.bin':
        '5cef3d2fa93610028dcb9a92c3d09949e6b56de79ff0d05050b61a57ff86fb15',
    'keysteppro_ui__fw0_0_1_1823__2024_03_04.bin':
        '145874ddc9762e31376fe615e90ff6605adcefccdb003c28f110b945dd5fd8e2',
    'keysteppro_main_chroma__fw2_5_20_375__2024_03_04.bin':
        'cfc18db3e7f2c54ad0b123a3a8efc6adf75ddaef9789230ad8e0285585fe6616',
    'keysteppro_ui_chroma__fw0_0_1_336__2024_03_03.bin':
        '3e63b798c18faa5c15ebdcf6e6f533fb49de8e3c3e2e243e4fa0b035cb0c1fb9',
}
HDR = 64


def load_space():
    with open(os.path.join(HERE, 'free_space.json')) as f:
        return json.load(f)


def image_space(space, image):
    """The measured free flash and RAM of one firmware image (free_space.json: the main image at the top level, the
    others under "images")."""
    if image == space['firmware']:
        return space
    for s in space.get('images', []):
        if s['firmware'] == image:
            return s
    raise SystemExit('no measured free space for %s' % image)


def parts(meta):
    """What a patch writes, one entry per firmware image: for each model it supports ("models": the standard KeyStep
    Pro, the Chroma, each with its own build of the main firmware), its main-MCU part (the manifest's top level) and any
    further "parts" (e.g. the UI MCU's). Per model each names its image, place and firmware symbols, and each hook
    its address ("at": {model: address}); the sources are shared and see KSP_CHROMA."""
    out = []
    for model in meta['models']:
        for p in [meta] + meta.get('parts', []):
            m = p['models'][model]
            out.append(dict({k: v for k, v in p.items() if k not in ('models', 'parts')}, model=model,
                            image=m['image'], place=m['place'], symbols=m.get('symbols', {}),
                            hooks=[dict(h, at=h['at'][model]) for h in p['hooks']]))
    return out


def asm(src, addr):
    from keystone import Ks, KS_ARCH_ARM, KS_MODE_THUMB
    enc, _ = Ks(KS_ARCH_ARM, KS_MODE_THUMB).asm(src, addr)
    return bytes(enc or b'')


def strip(src):
    return '\n'.join(l.split('@', 1)[0].rstrip() for l in src.splitlines() if l.split('@', 1)[0].strip())


def labels(src, addr):
    """Address of every top-level label: assemble the source up to it and take the length. Labels defined
    further on are appended as empty definitions so forward branches in the prefix resolve (every instruction
    used has a fixed width, so that does not change the length)."""
    lines, out = src.splitlines(), {}
    names = [m.group(1) for m in (re.match(r'^([A-Za-z_]\w*):\s*$', l) for l in lines) if m]
    for i, l in enumerate(lines):
        m = re.match(r'^([A-Za-z_]\w*):\s*$', l)
        if m:
            later = [n + ':' for n in names[names.index(m.group(1)):]]
            out[m.group(1)] = addr + len(asm('\n'.join(lines[:i] + later), addr))
    return out


def fill(src, values):
    """{NAME} -> 0x...; {NAME_LO}/{NAME_HI} -> the 16-bit halves (for movw/movt)."""
    def rep(m):
        name = m.group(1)
        for suffix, f in (('_LO', lambda v: v & 0xFFFF), ('_HI', lambda v: v >> 16)):
            if name.endswith(suffix) and name[:-len(suffix)] in values:
                return '0x%X' % f(values[name[:-len(suffix)]])
        if name in values:
            return '0x%X' % values[name]
        raise SystemExit('unknown symbol {%s}' % name)
    return re.sub(r'\{(\w+)\}', rep, src)


def ranges_overlap(a, b):
    return a[0] < b[1] and b[0] < a[1]


def arm_gnu(tool):
    """Path of an Arm GNU Toolchain program: ARM_GNU_BIN, the winget install folder, or PATH."""
    import glob, shutil
    dirs = [os.environ.get('ARM_GNU_BIN', '')] + sorted(glob.glob(
        os.path.join('C:' + os.sep, 'Program Files*', 'Arm GNU Toolchain arm-none-eabi', '*', 'bin')), reverse=True)
    for d in dirs:
        exe = os.path.join(d, 'arm-none-eabi-' + tool + ('.exe' if os.name == 'nt' else ''))
        if d and os.path.exists(exe):
            return exe
    exe = shutil.which('arm-none-eabi-' + tool)
    if not exe:
        raise SystemExit('arm-none-eabi-%s not found: install the Arm GNU Toolchain or set ARM_GNU_BIN' % tool)
    return exe


def build_c(name, meta, d):
    """Compile and link a C patch at its fixed place: code and constants in its flash slot, variables in its RAM slot.
    There is no startup code and that RAM is not cleared at boot, so initialised data is refused (link-time
    assert); a patch sets up its own variables (and must check a marker before trusting them). A part with no "ram"
    in its place may have no variables at all. "cpu" is the core it runs on (the UI MCU is a Cortex-M0). Returns
    (code bytes, {symbol: address}, ram end)."""
    import subprocess, tempfile
    pl = meta['place']
    flash, ram = int(pl['flash'], 16), int(pl.get('ram', '0x20000000'), 16)
    ram_size = int(pl.get('ram_size', '0'), 0)
    out = tempfile.mkdtemp(prefix='kspatch_%s_' % name)
    ld = os.path.join(out, 'patch.ld')
    with open(ld, 'w') as f:
        f.write("""MEMORY { FLASH (rx) : ORIGIN = 0x%08X, LENGTH = %d
         RAM (rw)   : ORIGIN = 0x%08X, LENGTH = %d }
SECTIONS {
  .text : { *(.text.patch_entry*) *(.text*) *(.rodata*) } > FLASH
  .data : { *(.data*) } > RAM
  .bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM
  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
ASSERT(SIZEOF(.data) == 0, "initialised data is not allowed in a patch: no startup code copies it")
""" % (flash, int(pl['flash_size'], 0), ram, max(ram_size, 4)))
        if not ram_size:
            f.write('ASSERT(SIZEOF(.bss) == 0, "this part has no RAM")\n')
    common = ['-mcpu=' + meta.get('cpu', 'cortex-m4'), '-mthumb', '-mfloat-abi=soft', '-Os', '-ffreestanding', '-fno-common',
              '-ffunction-sections', '-fdata-sections', '-Wall', '-Wextra', '-Werror',
              '-DKSP_CHROMA=%d' % (meta['model'] == 'chroma'), '-I', os.path.join(HERE, 'include'), '-I', d]
    objs = []
    for src in meta['sources']:
        o = os.path.join(out, os.path.basename(src) + '.o')
        subprocess.run([arm_gnu('gcc')] + common + ['-c', os.path.join(d, src), '-o', o], check=True)
        objs.append(o)
    elf = os.path.join(out, name + '.elf')
    subprocess.run([arm_gnu('gcc')] + common + ['-nostdlib', '-T', ld, '-Wl,--gc-sections',
                    '-Wl,--entry=0'] + [x for h in meta['hooks'] for x in ('-Wl,--undefined=' + h['asm'].split()[-1],)]
                   + objs + ['-lgcc', '-o', elf], check=True)
    binf = os.path.join(out, name + '.bin')
    subprocess.run([arm_gnu('objcopy'), '-O', 'binary', '-j', '.text', elf, binf], check=True)
    code = open(binf, 'rb').read()
    syms = {}
    for line in subprocess.run([arm_gnu('nm'), elf], capture_output=True, text=True, check=True).stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            syms[parts[2]] = int(parts[0], 16)
    bss_end = syms.get('__bss_end__', None)
    sizes = subprocess.run([arm_gnu('size'), '-A', elf], capture_output=True, text=True, check=True).stdout
    bss = int(re.search(r'^\.bss\s+(\d+)', sizes, re.M).group(1)) if re.search(r'^\.bss\s', sizes, re.M) else 0
    return code, syms, ram + bss


def assemble_part(name, meta, d, space):
    """One image's part of a patch, assembled at the fixed place it declares: its writes (address, our bytes), the
    flash and RAM it occupies and the instructions it hooks."""
    space = image_space(space, meta['image'])
    pl = meta['place']
    flash = int(pl['flash'], 16)
    ram = int(pl['ram'], 16) if 'ram' in pl else None
    values = {k: int(v, 16) for k, v in meta.get('symbols', {}).items()}
    if 'sources' in meta:
        code, lab, r = build_c(name, meta, d)
        lab = {k: v & ~1 for k, v in lab.items()}
        r = r if ram is not None else None
    else:
        src = strip(open(os.path.join(d, meta['source'])).read())
        r = ram
        for sym, n in meta.get('ram', {}).items():
            values[sym] = r
            r = (r + n + 3) & ~3
        code = asm(fill(src, values), flash)
        lab = labels(fill(src, values), flash)
    lo, hi = (int(x, 16) for x in space['flash'])
    if not (lo <= flash and flash + len(code) <= hi):
        raise SystemExit('%s: code 0x%08X..0x%08X is outside the free flash' % (name, flash, flash + len(code)))
    if ram is not None and not any(int(a_, 16) <= ram and r <= int(b_, 16) for a_, b_ in space['ram']):
        raise SystemExit('%s: RAM 0x%08X..0x%08X is outside the free RAM' % (name, ram, r))
    ram_note = ('none' if ram is None else '0x%08X..0x%08X' % (ram, r) if 'sources' in meta else
                ', '.join('%s=0x%08X' % (k, values[k]) for k in meta['ram']))
    writes = [(flash, code)]
    log = ['%s [%s]: code %d bytes at 0x%08X; RAM %s' % (name, meta['image'], len(code), flash, ram_note)]
    for h in meta['hooks']:
        at, target = int(h['at'], 16), h['asm'].split()[-1]
        ins = asm(fill(h['asm'].replace(target, '0x%X' % lab[target]), values), at)
        if len(ins) != 4:
            raise SystemExit('hook at 0x%08X assembled to %d bytes' % (at, len(ins)))
        writes.append((at, ins))
        log.append('  hook 0x%08X: %s -> 0x%08X (%s)' % (at, h['asm'], lab[target], h['what']))
    return {'image': meta['image'], 'model': meta['model'], 'writes': writes, 'flash': (flash, flash + len(code)),
            'ram': (ram, r) if ram is not None else (0, 0), 'hooks': [int(h['at'], 16) for h in meta['hooks']],
            'log': log}


def assemble(name, space):
    """One patch: each of its parts (one per firmware image it writes into) assembled at its fixed place. The web
    front end (web/) applies exactly these writes, exported by --export; nothing in it is the manufacturer's."""
    d = os.path.join(HERE, name)
    meta = json.load(open(os.path.join(d, 'patch.json')))
    return {'name': name, 'description': meta['description'], 'firmware': meta['firmware'],
            'parts': [assemble_part(name, m, d, space) for m in parts(meta)]}


def apply(payload, base, patches, log):
    """Apply assembled parts of patches (assemble_part(), all for one image, each tagged with its patch's 'name')
    to the stock payload loaded at `base`. Two patches may not hook the same instruction or share flash or RAM;
    flash past the image that no patch writes stays erased (0xFF)."""
    payload = bytearray(payload)
    for i, p in enumerate(patches):
        for q in patches[:i]:
            for what in ('flash', 'ram'):
                if ranges_overlap(p[what], q[what]):
                    raise SystemExit('%s and %s overlap in %s' % (q['name'], p['name'], what))
            both = set(p['hooks']) & set(q['hooks'])
            if both:
                raise SystemExit('%s and %s both hook 0x%08X' % (q['name'], p['name'], min(both)))
    for p in patches:
        log.extend(p['log'])
        for at, data in p['writes']:
            o = at - base
            if o + len(data) > len(payload):
                payload.extend(bytes([0xFF]) * (o + len(data) - len(payload)))
            payload[o:o + len(data)] = data
    return bytes(payload)


def export(names, out_dir):
    """--export: each patch as web/patches/<name>.json (our bytes only) and an index.json listing them."""
    space = load_space()
    os.makedirs(out_dir, exist_ok=True)
    index = []
    for n in names:
        p = assemble(n, space)
        meta = json.load(open(os.path.join(HERE, n, 'patch.json')))
        bundle = {'name': n, 'title': meta.get('title', n), 'pitch': meta.get('pitch', p['description']),
                  'description': p['description'], 'firmware': p['firmware'],
                  'parts': [{'model': q['model'], 'image': q['image'], 'sha256': STOCK[q['image']],
                             'writes': [{'at': '0x%08X' % at, 'bytes': data.hex()} for at, data in q['writes']],
                             'flash': ['0x%08X' % x for x in q['flash']], 'ram': ['0x%08X' % x for x in q['ram']],
                             'hooks': ['0x%08X' % h for h in q['hooks']]} for q in p['parts']]}
        with open(os.path.join(out_dir, n + '.json'), 'w') as f:
            json.dump(bundle, f, indent=1)
        index.append({'name': n, 'description': p['description'], 'file': n + '.json'})
        print('exported %s: %s' % (n, ', '.join('%d writes to %s' % (len(q['writes']), q['image']) for q in p['parts'])))
    with open(os.path.join(out_dir, 'index.json'), 'w') as f:
        json.dump({'patches': index}, f, indent=1)
    # the same bundles as a script, so web/index.html also works opened straight from disk (no server, no fetch)
    bundles = [json.load(open(os.path.join(out_dir, e['file']))) for e in index]
    with open(os.path.join(out_dir, 'bundles.js'), 'w') as f:
        f.write('// GENERATED by patches/make_patch.py --export - do not edit.\n')
        f.write('window.KSP_PATCHES = %s;\n' % json.dumps(bundles, indent=1))


def header(hdr, payload):
    h = bytearray(hdr)
    struct.pack_into('<I', h, 5, len(payload))
    h[9] = (-sum(payload)) & 0xFF                 # payload checksum: payload bytes sum to -b9
    h[63] = (-sum(h[:63])) & 0xFF                 # header checksum: the 64 header bytes sum to 0
    return bytes(h)


def build(kspf, names, out_dir, show_diff=False):
    z = zipfile.ZipFile(kspf)
    info = json.loads(z.read('info.json'))
    space = load_space()
    by_image = {}                                   # image -> the parts of the chosen patches that write into it
    for n in names:
        for q in assemble(n, space)['parts']:
            by_image.setdefault(q['image'], []).append(dict(q, name=n))
    for image in by_image:
        if hashlib.sha256(z.read(image)).hexdigest() != STOCK.get(image):
            raise SystemExit('%s in %s is not the stock firmware these patches were made for - refusing' % (image, kspf))
    log, patched, sizes = [], {}, []
    for image, ps in by_image.items():
        stock = z.read(image)
        size = struct.unpack_from('<I', stock, 5)[0]
        base = struct.unpack_from('<I', stock, 11)[0]
        old = stock[HDR:HDR + size]
        new = apply(old, base, ps, log)
        patched[image] = header(stock[:HDR], new) + new
        sizes.append('image %s: %d -> %d bytes' % (image, size, len(new)))
        if show_diff:
            for i in range(len(new)):
                if i >= len(old) or old[i] != new[i]:
                    log.append('  %s 0x%08X  %s -> %02X' % (image[:15], base + i, '%02X' % old[i] if i < len(old) else '--',
                                                         new[i]))
    tag = '+'.join(names)
    info['version_number'] = '%s+%s' % (info['version_number'], tag)
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, 'keystep-pro_Firmware_%s.kspf' % info['version_number'])
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_STORED) as o:
        for e in z.infolist():
            data = json.dumps(info, indent=4).encode() if e.filename == 'info.json' else (
                patched.get(e.filename) or z.read(e.filename))
            o.writestr(e, data)
    for l in log + sizes:
        print(l)
    print('written %s' % out)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('patches', nargs='*')
    ap.add_argument('--kspf', default=DEFAULT_KSPF)
    ap.add_argument('--out', default=None)
    ap.add_argument('--diff', action='store_true')
    ap.add_argument('--export', action='store_true', help='write web/patches/*.json for the web front end')
    a = ap.parse_args()
    if a.export:
        names = a.patches or sorted(d for d in os.listdir(HERE) if os.path.exists(os.path.join(HERE, d, 'patch.json')))
        return export(names, os.path.join(os.path.dirname(HERE), 'web', 'patches'))
    if not a.patches:
        ap.error('name at least one patch')
    build(a.kspf, a.patches, a.out or os.getcwd(), a.diff)


if __name__ == '__main__':
    sys.exit(main())
