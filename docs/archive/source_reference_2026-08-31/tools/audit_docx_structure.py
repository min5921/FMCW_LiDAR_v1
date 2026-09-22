#!/usr/bin/env python3
"""Audit the fixed layout contract of the generated DOCX reference manuals."""

from __future__ import annotations

import argparse
import json
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET


W = "{http://schemas.openxmlformats.org/wordprocessingml/2006/main}"


def attr(element: ET.Element | None, name: str) -> str | None:
    return None if element is None else element.get(W + name)


def child(element: ET.Element | None, name: str) -> ET.Element | None:
    return None if element is None else element.find(W + name)


def audit_document(path: Path) -> dict:
    issues: list[str] = []
    with zipfile.ZipFile(path) as archive:
        document = ET.fromstring(archive.read("word/document.xml"))
        styles = ET.fromstring(archive.read("word/styles.xml"))
        numbering = ET.fromstring(archive.read("word/numbering.xml"))

    section = document.find(f".//{W}sectPr")
    page_size = child(section, "pgSz")
    page_margin = child(section, "pgMar")
    expected_page = {"w": "12240", "h": "15840"}
    expected_margin = {
        "top": "1440", "right": "1440", "bottom": "1440", "left": "1440"
    }
    for name, expected in expected_page.items():
        if attr(page_size, name) != expected:
            issues.append(f"page {name}: {attr(page_size, name)} != {expected}")
    for name, expected in expected_margin.items():
        if attr(page_margin, name) != expected:
            issues.append(f"margin {name}: {attr(page_margin, name)} != {expected}")

    style_expectations = {
        "Normal": {"size": "22", "after": "120", "line": "300"},
        "Heading1": {"size": "32", "before": "360", "after": "200"},
        "Heading2": {"size": "26", "before": "280", "after": "140"},
        "Heading3": {"size": "22", "before": "200", "after": "100"},
    }
    styles_by_id = {attr(style, "styleId"): style for style in styles.findall(f"{W}style")}
    for style_id, expected in style_expectations.items():
        style = styles_by_id.get(style_id)
        if style is None:
            issues.append(f"missing style {style_id}")
            continue
        paragraph = child(style, "pPr")
        run = child(style, "rPr")
        spacing = child(paragraph, "spacing")
        size = child(run, "sz")
        values = {
            "size": attr(size, "val"),
            "before": attr(spacing, "before"),
            "after": attr(spacing, "after"),
            "line": attr(spacing, "line"),
        }
        for name, expected_value in expected.items():
            if values[name] != expected_value:
                issues.append(
                    f"{style_id} {name}: {values[name]} != {expected_value}"
                )
        if style_id == "Heading2" and child(paragraph, "pageBreakBefore") is None:
            issues.append("Heading2 must start on a new page")

    tables = document.findall(f".//{W}tbl")
    for index, table in enumerate(tables, start=1):
        properties = child(table, "tblPr")
        width = child(properties, "tblW")
        indent = child(properties, "tblInd")
        grid_widths = [int(attr(col, "w") or 0) for col in table.findall(f"{W}tblGrid/{W}gridCol")]
        if attr(width, "type") != "dxa" or attr(width, "w") != "9360":
            issues.append(f"table {index}: width is not fixed at 9360 dxa")
        if attr(indent, "w") != "120":
            issues.append(f"table {index}: indent is not 120 dxa")
        if sum(grid_widths) != 9360:
            issues.append(f"table {index}: grid sum {sum(grid_widths)} != 9360")

    bullet_levels = []
    for level in numbering.findall(f".//{W}lvl"):
        number_format = child(level, "numFmt")
        if attr(number_format, "val") == "bullet":
            bullet_levels.append(level)
    matching_bullets = []
    for level in bullet_levels:
        paragraph = child(level, "pPr")
        indent = child(paragraph, "ind")
        spacing = child(paragraph, "spacing")
        if (
            attr(indent, "left") == "540"
            and attr(indent, "hanging") == "270"
            and attr(spacing, "after") == "80"
            and attr(spacing, "line") == "300"
        ):
            matching_bullets.append(level)
    if not bullet_levels:
        issues.append("custom bullet numbering is missing")
    elif not matching_bullets:
        issues.append("custom bullet spacing and indent contract is missing")

    return {
        "document": path.name,
        "tables": len(tables),
        "issues": issues,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("document_dir", type=Path)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    reports = [audit_document(path) for path in sorted(args.document_dir.glob("*.docx"))]
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(reports, ensure_ascii=False, indent=2), encoding="utf-8")
    for report in reports:
        print(f"{report['document']}: {report['tables']} tables, {len(report['issues'])} issues")
    return 1 if any(report["issues"] for report in reports) else 0


if __name__ == "__main__":
    raise SystemExit(main())
