#!/usr/bin/env python3
"""Build a tiny ESKLP KLP fixture (multi-row header + numbering row) for
test_registry streaming checks. Cells use inlineStr so no sharedStrings table
is needed.
"""

import sys
import zipfile

HEADER = [
    "код узла смнн",
    "стандартизованное мнн",
    "код клп",
    "торговое наименование",
    "нормализованное мнн",
    "предельная отпускная цена на позицию клп",
]

DATA = [
    ["21.20.10.194-000029-1-00109-0000000000000", "АБАКАВИР", "KLP-0001",
     "Абакавир 500 мг таблетки", "абакавир", "1200"],
    ["21.20.10.194-000094-1-00003-0000000000000", "ВИРКОМБО", "KLP-0002",
     "Виркомбо таблетки", "виркомбо", "3400"],
    ["", "", "KLP-0003", "Бинт стерильный", "бинт", "40"],
    ["21.20.10.251-000777-1-00005-0000000000000", "ГОМЕОПАТИЯ", "KLP-0004",
     "Гомеопатическое средство", "гомеопатическое средство", "100"],
]


def letters(index):
    out = ""
    index += 1
    while index > 0:
        rem = (index - 1) % 26
        out = chr(65 + rem) + out
        index = (index - 1) // 26
    return out


def cell(row, col, value):
    if value == "":
        return ""
    return '<c r="%s%d" t="inlineStr"><is><t>%s</t></is></c>' % (letters(col), row, value)


def main():
    dst = sys.argv[1] if len(sys.argv) > 1 else "tests/fixtures/esklp_klp_sample.xlsx"
    rows = []
    rows.append('<row r="1">%s</row>' % "".join(cell(1, i, h) for i, h in enumerate(HEADER)))
    rows.append('<row r="2">%s</row>' % "".join(cell(2, i, "%d.0" % (i + 1)) for i in range(len(HEADER))))
    for i, data in enumerate(DATA):
        r = 3 + i
        rows.append('<row r="%d">%s</row>' % (r, "".join(cell(r, c, v) for c, v in enumerate(data))))

    sheet_xml = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        '<dimension ref="A1:F%d"/>'
        '<sheetData>%s</sheetData>'
        '</worksheet>'
    ) % (2 + len(DATA), "".join(rows))

    content_types = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
        '<Override PartName="/xl/worksheets/sheet1.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>'
        '</Types>'
    )
    workbook = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        '<sheets><sheet name="esklp_klp_sample" r:id="rId1" sheetId="1"/></sheets></workbook>'
    )
    rels = (
        '<?xml version="1.0" encoding="UTF-8"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet1.xml"/>'
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
        out.writestr("xl/worksheets/sheet1.xml", sheet_xml)

    print("rows=%d data=%d -> %s" % (2 + len(DATA), len(DATA), dst))


if __name__ == "__main__":
    main()
