// node web/test_kspatch.js [stock.kspf] [make_patch-output.kspf]
// The web patcher against patches/make_patch.py: built from the same stock file with the same patches, every entry
// of the two updates must be byte-identical. Also: SHA-256 against a known vector, a stock file with one byte changed
// must be refused, and two patches that overlap must be refused.
'use strict';
const fs = require('fs'), path = require('path'), cp = require('child_process'), os = require('os');
const K = require('./kspatch.js');

const STOCK = process.argv[2] || 'C:\\ProgramData\\Arturia\\MIDI Control Center\\Firmware\\keystep-pro_Firmware_Update_2.5.20.0.kspf';
const bundles = JSON.parse(fs.readFileSync(path.join(__dirname, 'patches', 'index.json'))).patches
    .map(e => JSON.parse(fs.readFileSync(path.join(__dirname, 'patches', e.file))));
let fails = 0;
const check = (ok, what) => { console.log((ok ? 'PASS ' : 'FAIL ') + what); if (!ok) fails++; };

(async () => {
    check(K.sha256hex(Buffer.from('abc')) === 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
          'SHA-256 of "abc"');
    const stock = new Uint8Array(fs.readFileSync(STOCK));
    const web = await K.build(stock, bundles);

    let ref = process.argv[3];
    if (!ref) {
        const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'kspatch_'));
        cp.execFileSync('python', [path.join(__dirname, '..', 'patches', 'make_patch.py'), ...bundles.map(b => b.name),
                                   '--kspf', STOCK, '--out', dir], { stdio: 'ignore' });
        ref = path.join(dir, web.filename);
    }
    const a = await K.unzip(web.bytes), b = await K.unzip(new Uint8Array(fs.readFileSync(ref)));
    check(a.map(e => e.name).join() === b.map(e => e.name).join(), 'same entries in the same order: ' + a.map(e => e.name).join(', '));
    for (const e of a) {
        const r = b.find(x => x.name === e.name);
        check(r && Buffer.compare(Buffer.from(e.data), Buffer.from(r.data)) === 0,
              'entry ' + e.name + ' byte-identical to make_patch.py (' + e.data.length + ' bytes)');
    }
    check(web.filename === path.basename(ref), 'file name ' + web.filename);

    // must-fail: a stock file with one byte of the main image changed
    const s2 = await K.unzip(stock);
    const img = s2.find(e => e.name === bundles[0].parts[0].image);
    img.data[1000] ^= 1;
    let refused = false;
    try { await K.build(K.zip(s2), bundles); } catch (e) { refused = /not the stock/.test(e.message); }
    check(refused, 'a non-stock image is refused');

    // must-fail: a stock file with one byte of a second image (the UI MCU's) changed
    const multi = bundles.find(b => b.parts.length > 1);
    if (multi) {
        const s3 = await K.unzip(stock);
        s3.find(e => e.name === multi.parts[1].image).data[1000] ^= 1;
        refused = false;
        try { await K.build(K.zip(s3), [multi]); } catch (e) { refused = /not the stock/.test(e.message); }
        check(refused, 'a non-stock ' + multi.parts[1].image + ' is refused');
    }

    // must-fail: two patches that share flash
    const clash = JSON.parse(JSON.stringify(bundles[0]));
    clash.name = 'clash'; clash.parts[0].hooks = ['0x08000000'];
    refused = false;
    try { await K.build(stock, [bundles[0], clash]); } catch (e) { refused = /overlap/.test(e.message); }
    check(refused, 'two patches that overlap are refused');

    console.log(fails ? 'RESULT FAIL' : 'RESULT PASS');
    process.exit(fails ? 1 : 0);
})().catch(e => { console.error(e); process.exit(2); });
