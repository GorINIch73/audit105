#!/usr/bin/env python3
"""Trim a real ESKLP export into a small XLSX fixture for test_registry.

Header rows, merges and cell types are kept verbatim; only data rows are
selected and the shared-string table is filtered to the strings actually used.
"""

import re
import sys
import zipfile

HEADER_ROWS = 4

ART = ("J05AF", "J05AG", "J05AR", "J05AJ")
TB = ("J04A",)
VACC = ("J07",)
OKPD = ("21.20.23.110", "26.60.12.119")
MASKS = ("наркотическ", "опиат", "каннабиноид", "алкотест", "этанол", "хти")

CELL_RE = re.compile(r"<c\b[^>]*/>|<c\b[^>]*>.*?</c>", re.S)
ROW_RE = re.compile(r"<row\b[^>]*>(.*?)</row>", re.S)


def cell_text(cell_xml, shared):
    match = re.search(r"<v>(.*?)</v>", cell_xml, re.S)
    if not match:
        return ""
    raw = match.group(1)
    if 't="s"' in cell_xml:
        return re.sub(r"<[^>]+>", "", shared[int(raw)])
    return raw


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else "esklp/esklp_smnn_20261014.xlsx"
    dst = sys.argv[2] if len(sys.argv) > 2 else "tests/fixtures/esklp_smnn_sample.xlsx"

    with zipfile.ZipFile(src) as archive:
        shared = re.findall(r"<si>(.*?)</si>", archive.read("xl/sharedStrings.xml").decode("utf-8"), re.S)
        sheet = archive.read("xl/worksheets/sheet2.xml").decode("utf-8")
        merges = re.findall(r'<mergeCell ref="([^"]+)"', sheet)

    rows = ROW_RE.findall(sheet)

def column_index(ref):
    letters = re.match(r"([A-Z]+)", ref).group(1)
    index = 0
    for char in letters:
        index = index * 26 + (ord(char) - 64)
    return index - 1


def cell_values(cells, shared):
    values = {}
    for cell in cells:
        match = re.search(r'r="([A-Z]+)\d+"', cell)
        if not match:
            continue
        values[column_index(match.group(1))] = cell_text(cell, shared)
    return values


def tags(cells, shared):
    values = cell_values(cells, shared)
    atx = values.get(13, "").upper()
    okpd = values.get(2, "")
    name = values.get(0, "").lower()
    if any(p in atx for p in ART):
        return "ART"
    if any(p in atx for p in TB):
        return "TB"
    if any(p in atx for p in VACC):
        return "VACC"
    if any(p in okpd for p in OKPD) or any(m in name for m in MASKS):
        return "TEST"
    return None

    selected = []
    used = set()
    perTag = {}
    for index, row_xml in enumerate(rows):
        number = index + 1
        cells = CELL_RE.findall(row_xml)
        if number <= HEADER_ROWS:
            selected.append((number, cells, None))
            continue
        tag = tags(cells, shared)
        if number <= 40 or (tag and perTag.get(tag, 0) < 40):
            selected.append((number, cells, tag))
            used.add(tag)
            if tag:
                perTag[tag] = perTag.get(tag, 0) + 1

    remap = {}
    kept = []
    for _, cells, _ in selected:
        for cell in cells:
            match = re.search(r"<v>(.*?)</v>", cell, re.S)
            if match and 't="s"' in cell:
                old = int(match.group(1))
                if old not in remap:
                    remap[old] = len(kept)
                    kept.append(shared[old])

    out_rows = []
    for number, cells, _ in selected:
        rebuilt = []
        for cell in cells:
            match = re.search(r"<v>(.*?)</v>", cell, re.S)
            if match and 't="s"' in cell:
                old = int(match.group(1))
                new = re.sub(r"<v>.*?</v>", "<v>%d</v>" % remap[old], cell, count=1, flags=re.S)
                rebuilt.append(new)
            else:
                rebuilt.append(cell)
        out_rows.append('<row r="%d">%s</row>' % (number, "".join(rebuilt)))

    shared_xml = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<sst xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'count="%d" uniqueCount="%d">%s</sst>'
    ) % (len(kept), len(kept), "".join("<si>%s</si>" % s for s in kept))

    sheet_xml = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        '<dimension ref="A1:AK%d"/>'
        '<mergeCells count="%d">%s</mergeCells>'
        '<sheetData>%s</sheetData>'
        '</worksheet>'
    ) % (len(out_rows), len(merges), "".join('<mergeCell ref="%s"/>' % m for m in merges), "".join(out_rows))

    content_types = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
        '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        '<Override PartName="/xl/sharedStrings.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sharedStrings+xml"/>'
        '</Types>'
    )
    workbook = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        '<sheets><sheet name="esklp_smnn_sample" r:id="rId1" sheetId="1"/></sheets></workbook>'
    )
    rels = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>'
        '<Relationship Id="rId2" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/sharedStrings" Target="sharedStrings.xml"/>'
        '</Relationships>'
    )
    root_rels = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>'
        '</Relationships>'
    )

    with zipfile.ZipFile(dst, "w", zipfile.ZIP_DEFLATED) as out:
        out.writestr("[Content_Types].xml", content_types)
        out.writestr("_rels/.rels", root_rels)
        out.writestr("xl/workbook.xml", workbook)
        out.writestr("xl/_rels/workbook.xml.rels", rels)
        out.writestr("xl/sharedStrings.xml", shared_xml)
        out.writestr("xl/worksheets/sheet1.xml", sheet_xml)

    print("rows=%d data=%d tags=%s -> %s" % (len(out_rows), len(selected) - HEADER_ROWS,
                                             sorted(t for t in used if t), dst))


if __name__ == "__main__":
    main()
