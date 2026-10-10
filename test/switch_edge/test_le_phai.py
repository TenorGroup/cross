"""Moi phan tu cuoi dong cua danh sach (cong tac, dau ^v cua pop-up, dau ">") ket thuc o MOT cot muc phai.

Chay gia lap X3 (pio run -e simulator_x3_uc8279) va X4 Pro (pio run -e simulator_x4pro) qua moi man co cong tac
trong Cai dat, Cai dat van ban, Dong ho, KOReader, OPDS, Bluetooth, menu doc, o ca 3 co chu, roi do tren anh 1 bit:
- mep phai khoi muc cua cong tac = mep phai khoi muc cua dau ^v = mep phai khoi muc cua dau ">" (lech 0 px);
- cot do la cot cua dau ">" cua may: X4 Pro 443 (444 o co chu 2), X3 489 (490 o vai man co chu cuon);
- tam doc cong tac lech khong qua 0,5 px so voi tam dau ^v cua hang cung luoi (cung 1 ham dat tam hang).
X4PRO_PROGRAM, X3_PROGRAM chon ban dung khac; LE_PHAI_DEVICES (x3,x4) va LE_PHAI_SIZES (0,1,2) thu hep lan chay.
"""
import os
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import le_phai as L

COLUMN = {'x4': {443, 444}, 'x3': {489, 490}}
DEVICES = os.environ.get('LE_PHAI_DEVICES', 'x4,x3').split(',')
SIZES = [int(s) for s in os.environ.get('LE_PHAI_SIZES', '0,1,2').split(',')]


def check(device, size, name, folder, failures, counts):
    images = L.play(device, folder, L.screen_steps(device, size, name), size)
    for k, img in enumerate(images):
        elements = L.trailing_elements(img)
        where = f'{device} co{size} {name} anh{k}'
        edges = {e['x1'] for e in elements}
        if len(edges) > 1:
            failures.append(f'{where}: mep phai khong thang hang, cac cot {sorted(edges)}: ' +
                            ', '.join(f"{e['kind']}@{e['x1']}(y{e['cy']:.0f})" for e in elements))
        for e in elements:
            if e['x1'] not in COLUMN[device]:
                failures.append(f"{where}: {e['kind']} y{e['cy']:.0f} mep phai {e['x1']} ngoai cot {sorted(COLUMN[device])}")
            if e['kind'] != 'sw':
                continue
            counts['sw'] += 1
            if (e['x1'] - e['x0'] + 1, e['y1'] - e['y0'] + 1) not in L.SW_SIZES:
                failures.append(f'{where}: hinh cong tac doi {e}')
        counts['ends'] += len(elements)
        for e in elements:
            if e['kind'] != 'sw':
                continue
            r = L.vertical_offset(img, e, elements, device)
            if r is None:
                continue
            counts['vertical'] += 1
            if abs(r) > 0.5:
                failures.append(f"{where}: tam doc cong tac y{e['cy']} lech {r} px so voi dau ^v cung hang")


def main():
    failures, counts = [], dict(sw=0, ends=0, vertical=0)
    with tempfile.TemporaryDirectory(prefix='le-phai-') as tmp:
        jobs = [(d, s, n) for d in DEVICES for s in SIZES for n in L.screens(d, s)]
        with ThreadPoolExecutor(4) as pool:
            futures = [pool.submit(check, d, s, n, Path(tmp) / f'{d}_{s}_{i}', failures, counts)
                       for i, (d, s, n) in enumerate(jobs)]
            for f in futures:
                f.result()
    assert counts['sw'] >= 40, f"chi do duoc {counts['sw']} cong tac: bo man hinh hong"
    assert counts['vertical'] >= 20, f"chi so duoc {counts['vertical']} cong tac voi dau ^v cung hang"
    assert not failures, f"{len(failures)} loi\n" + '\n'.join(failures[:40])
    print(f"GREEN: {counts['sw']} cong tac, {counts['ends']} phan tu cuoi dong, {len(jobs)} man tren moi cot muc phai")


if __name__ == '__main__':
    main()
