// kspatch.js - build a patched KeyStep Pro firmware update from the user's own stock update file, in the browser.
//
// The same steps as patches/make_patch.py, on the same data: the patches are the bundles that
// `python patches/make_patch.py --export` writes (web/patches/*.json: each patch's own bytes and where they go),
// applied to the stock images they write into (main MCU, UI MCU) after each one's SHA-256 is checked; then each
// image header's size and two checksums are fixed, info.json's version names the patches, and the update is
// zipped again. Nothing is uploaded: the stock file is read, patched and saved on this machine.
// tests: node web/test_kspatch.js
(function (root) {
    'use strict';

    // ---- SHA-256 (FIPS 180-4), plain JS so the page works from a file:// URL too ----------------------------------
    const K = new Uint32Array([
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);

    function sha256hex(bytes) {
        const n = bytes.length, total = ((n + 9 + 63) >> 6) << 6;
        const m = new Uint8Array(total);
        m.set(bytes);
        m[n] = 0x80;
        const dv = new DataView(m.buffer);
        dv.setUint32(total - 8, Math.floor(n / 0x20000000), false);
        dv.setUint32(total - 4, (n * 8) >>> 0, false);
        const h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
        const w = new Uint32Array(64);
        const rotr = (x, r) => (x >>> r) | (x << (32 - r));
        for (let off = 0; off < total; off += 64) {
            for (let i = 0; i < 16; i++) w[i] = dv.getUint32(off + 4 * i, false);
            for (let i = 16; i < 64; i++) {
                const s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >>> 3);
                const s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >>> 10);
                w[i] = (w[i - 16] + s0 + w[i - 7] + s1) >>> 0;
            }
            let [a, b, c, d, e, f, g, hh] = h;
            for (let i = 0; i < 64; i++) {
                const t1 = (hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i]) >>> 0;
                const t2 = ((rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c))) >>> 0;
                hh = g; g = f; f = e; e = (d + t1) >>> 0; d = c; c = b; b = a; a = (t1 + t2) >>> 0;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
        return Array.from(h, x => x.toString(16).padStart(8, '0')).join('');
    }

    // ---- CRC-32 and the zip container (stored entries are written; stored or deflated ones are read) -------------
    const CRC = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
        let c = i;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        CRC[i] = c >>> 0;
    }
    function crc32(b) {
        let c = 0xffffffff;
        for (let i = 0; i < b.length; i++) c = CRC[(c ^ b[i]) & 0xff] ^ (c >>> 8);
        return (c ^ 0xffffffff) >>> 0;
    }

    async function inflateRaw(data) {
        if (typeof DecompressionStream === 'undefined') throw new Error('this browser cannot read a compressed zip entry');
        const s = new Blob([data]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
        return new Uint8Array(await new Response(s).arrayBuffer());
    }

    async function unzip(bytes) {
        const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
        let eocd = -1;
        for (let i = bytes.length - 22; i >= Math.max(0, bytes.length - 65557); i--)
            if (dv.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
        if (eocd < 0) throw new Error('not a zip file (a .kspf update is one)');
        const count = dv.getUint16(eocd + 10, true);
        let p = dv.getUint32(eocd + 16, true);
        const out = [];
        for (let k = 0; k < count; k++) {
            if (dv.getUint32(p, true) !== 0x02014b50) throw new Error('damaged zip directory');
            const method = dv.getUint16(p + 10, true), time = dv.getUint16(p + 12, true), date = dv.getUint16(p + 14, true);
            const csize = dv.getUint32(p + 20, true), nlen = dv.getUint16(p + 28, true);
            const elen = dv.getUint16(p + 30, true), clen = dv.getUint16(p + 32, true);
            const local = dv.getUint32(p + 42, true);
            const name = new TextDecoder().decode(bytes.subarray(p + 46, p + 46 + nlen));
            const lstart = local + 30 + dv.getUint16(local + 26, true) + dv.getUint16(local + 28, true);
            const raw = bytes.subarray(lstart, lstart + csize);
            let data;
            if (method === 0) data = raw.slice();
            else if (method === 8) data = await inflateRaw(raw);
            else throw new Error('zip entry ' + name + ' uses compression method ' + method);
            out.push({ name, time, date, data });
            p += 46 + nlen + elen + clen;
        }
        return out;
    }

    function zip(entries) {
        const enc = new TextEncoder(), parts = [], dir = [];
        let off = 0;
        for (const e of entries) {
            const name = enc.encode(e.name), crc = crc32(e.data), n = e.data.length;
            const lh = new Uint8Array(30), l = new DataView(lh.buffer);
            l.setUint32(0, 0x04034b50, true); l.setUint16(4, 20, true); l.setUint16(6, 0, true); l.setUint16(8, 0, true);
            l.setUint16(10, e.time, true); l.setUint16(12, e.date, true); l.setUint32(14, crc, true);
            l.setUint32(18, n, true); l.setUint32(22, n, true); l.setUint16(26, name.length, true); l.setUint16(28, 0, true);
            const ch = new Uint8Array(46), c = new DataView(ch.buffer);
            c.setUint32(0, 0x02014b50, true); c.setUint16(4, 20, true); c.setUint16(6, 20, true);
            c.setUint16(12, e.time, true); c.setUint16(14, e.date, true); c.setUint32(16, crc, true);
            c.setUint32(20, n, true); c.setUint32(24, n, true); c.setUint16(28, name.length, true);
            c.setUint32(42, off, true);
            parts.push(lh, name, e.data);
            dir.push(ch, name);
            off += 30 + name.length + n;
        }
        const dsize = dir.reduce((s, b) => s + b.length, 0);
        const end = new Uint8Array(22), d = new DataView(end.buffer);
        d.setUint32(0, 0x06054b50, true); d.setUint16(8, entries.length, true); d.setUint16(10, entries.length, true);
        d.setUint32(12, dsize, true); d.setUint32(16, off, true);
        const all = parts.concat(dir, [end]), out = new Uint8Array(off + dsize + 22);
        let p = 0;
        for (const b of all) { out.set(b, p); p += b.length; }
        return out;
    }

    // ---- the patch, as make_patch.py applies it -----------------------------------------------------------------
    const HDR = 64;
    const hex = x => parseInt(x, 16);
    const fromHex = s => Uint8Array.from(s.match(/../g) || [], b => parseInt(b, 16));
    const overlap = (a, b) => a[0] < b[1] && b[0] < a[1];

    // A patch writes into one or more firmware images (bundle.parts: the main MCU's, the UI MCU's), each checked
    // against its stock SHA-256 before anything is applied.
    const partsOf = bundles => {
        const byImage = new Map();
        for (const b of bundles)
            for (const p of b.parts) {
                if (!byImage.has(p.image)) byImage.set(p.image, { sha256: p.sha256, parts: [] });
                byImage.get(p.image).parts.push(Object.assign({ name: b.name }, p));
            }
        return byImage;
    };

    // the stock file: which images the patches are for, and whether each is the exact stock one
    async function inspect(kspfBytes, bundles) {
        const entries = await unzip(kspfBytes);
        const info = entries.find(e => e.name === 'info.json');
        if (!info) throw new Error('no info.json: this is not a KeyStep Pro firmware update');
        const images = [...partsOf(bundles)].map(([name, w]) => {
            const img = entries.find(e => e.name === name);
            return { name, stock: !!img && sha256hex(img.data) === w.sha256 };
        });
        return {
            entries,
            info: JSON.parse(new TextDecoder().decode(info.data)),
            stock: images.length > 0 && images.every(i => i.stock),
            images,
        };
    }

    function applyImage(stockImg, parts) {
        const rng = p => ({ flash: p.flash.map(hex), ram: p.ram.map(hex), hooks: p.hooks.map(hex) });
        for (let i = 0; i < parts.length; i++)
            for (let j = 0; j < i; j++) {
                const p = rng(parts[i]), q = rng(parts[j]);
                for (const what of ['flash', 'ram'])
                    if (overlap(p[what], q[what])) throw new Error(parts[j].name + ' and ' + parts[i].name + ' overlap in ' + what);
                if (p.hooks.some(h => q.hooks.includes(h)))
                    throw new Error(parts[j].name + ' and ' + parts[i].name + ' hook the same instruction');
            }
        const dv = new DataView(stockImg.buffer, stockImg.byteOffset, stockImg.byteLength);
        const size = dv.getUint32(5, true), base = dv.getUint32(11, true);
        let payload = Array.from(stockImg.subarray(HDR, HDR + size));
        for (const p of parts)
            for (const w of p.writes) {
                const o = hex(w.at) - base, data = fromHex(w.bytes);
                while (payload.length < o + data.length) payload.push(0xff);
                for (let k = 0; k < data.length; k++) payload[o + k] = data[k];
            }
        payload = Uint8Array.from(payload);
        const hdr = stockImg.slice(0, HDR), hv = new DataView(hdr.buffer);
        hv.setUint32(5, payload.length, true);
        hdr[9] = (-payload.reduce((s, x) => s + x, 0)) & 0xff;      // the payload's bytes sum to -byte 9
        hdr[63] = (-hdr.subarray(0, 63).reduce((s, x) => s + x, 0)) & 0xff;   // the header sums to 0
        const patched = new Uint8Array(HDR + payload.length);
        patched.set(hdr); patched.set(payload, HDR);
        return { patched, grown: payload.length - size };
    }

    async function build(kspfBytes, bundles) {
        if (!bundles.length) throw new Error('choose at least one patch');
        for (const b of bundles)
            if (b.firmware.version !== bundles[0].firmware.version)
                throw new Error(b.name + ' is for a different firmware');
        const { entries, info, images } = await inspect(kspfBytes, bundles);
        const bad = images.find(i => !i.stock);
        if (bad)
            throw new Error('this is not the stock ' + bundles[0].firmware.version + ' update these patches were made ' +
                'for (its ' + bad.name + ' does not match); use the untouched file from MIDI Control Center');
        const patched = new Map();
        let grown = 0;
        for (const [name, w] of partsOf(bundles)) {
            const r = applyImage(entries.find(e => e.name === name).data, w.parts);
            patched.set(name, r.patched);
            grown += r.grown;
        }
        const names = bundles.map(b => b.name);
        info.version_number = info.version_number + '+' + names.join('+');
        const infoBytes = new TextEncoder().encode(JSON.stringify(info, null, 4));
        const out = entries.map(e => ({ name: e.name, time: e.time, date: e.date,
            data: e.name === 'info.json' ? infoBytes : patched.get(e.name) || e.data }));
        return {
            bytes: zip(out),
            filename: 'keystep-pro_Firmware_' + info.version_number + '.kspf',
            version: info.version_number,
            grown,
        };
    }

    const api = { sha256hex, crc32, unzip, zip, inspect, build };
    if (typeof module !== 'undefined' && module.exports) module.exports = api;
    else root.KSPatch = api;
})(typeof window !== 'undefined' ? window : globalThis);
