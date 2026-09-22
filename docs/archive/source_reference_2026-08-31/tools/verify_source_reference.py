#!/usr/bin/env python3
"""Render-output QA for the generated source reference manuals."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

import pdfplumber
from PIL import Image, ImageChops, ImageDraw, ImageFont


def page_number(path: Path) -> int:
    match = re.search(r"(\d+)$", path.stem)
    return int(match.group(1)) if match else 0


def content_bbox(image: Image.Image):
    rgb = image.convert("RGB")
    background = Image.new("RGB", rgb.size, "white")
    difference = ImageChops.difference(rgb, background)
    return difference.getbbox()


def make_contact_sheet(paths: list[Path], output: Path, columns: int, thumb_width: int,
                       selected: list[int] | None = None) -> None:
    indices = selected if selected is not None else list(range(len(paths)))
    if not indices:
        return
    images = []
    for index in indices:
        image = Image.open(paths[index]).convert("RGB")
        height = round(image.height * thumb_width / image.width)
        image.thumbnail((thumb_width, height), Image.Resampling.LANCZOS)
        canvas = Image.new("RGB", (thumb_width + 10, height + 34), "#d9dee5")
        canvas.paste(image, (5, 25))
        draw = ImageDraw.Draw(canvas)
        draw.text((8, 6), f"Page {index + 1}", fill="#18202a")
        images.append(canvas)
    rows = (len(images) + columns - 1) // columns
    cell_width = max(image.width for image in images)
    cell_height = max(image.height for image in images)
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), "#bcc3cc")
    for index, image in enumerate(images):
        x = (index % columns) * cell_width
        y = (index // columns) * cell_height
        sheet.paste(image, (x, y))
    output.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(output)


def inspect_document(directory: Path, qa_dir: Path) -> dict:
    pdfs = list(directory.glob("*.pdf"))
    if len(pdfs) != 1:
        return {"document": directory.name, "error": f"expected one PDF, found {len(pdfs)}"}
    pngs = sorted(directory.glob("page-*.png"), key=page_number)
    anomalies = []
    page_stats = []
    with pdfplumber.open(pdfs[0]) as pdf:
        if len(pdf.pages) != len(pngs):
            anomalies.append(f"PDF pages {len(pdf.pages)} != PNG pages {len(pngs)}")
        for index, page in enumerate(pdf.pages):
            chars = page.chars
            text = page.extract_text() or ""
            if len(text.strip()) < 10:
                anomalies.append(f"page {index + 1}: nearly blank ({len(text.strip())} chars)")
            edge_chars = [
                char for char in chars
                if char.get("x0", 0) < 3 or char.get("x1", 0) > page.width - 3
                or char.get("top", 0) < 3 or char.get("bottom", 0) > page.height - 3
            ]
            if edge_chars:
                anomalies.append(f"page {index + 1}: {len(edge_chars)} glyphs touch page edge")
            page_stats.append({
                "page": index + 1,
                "characters": len(chars),
                "text_characters": len(text),
                "width_pt": round(page.width, 2),
                "height_pt": round(page.height, 2),
            })

    expected_size = None
    for index, png in enumerate(pngs):
        image = Image.open(png)
        if expected_size is None:
            expected_size = image.size
        elif image.size != expected_size:
            anomalies.append(f"page {index + 1}: PNG size {image.size} != {expected_size}")
        bbox = content_bbox(image)
        if bbox is None:
            anomalies.append(f"page {index + 1}: raster is blank")
            continue
        left, top, right, bottom = bbox
        if left <= 2 or top <= 2 or right >= image.width - 2 or bottom >= image.height - 2:
            anomalies.append(f"page {index + 1}: raster content touches image edge {bbox}")

    density_order = sorted(range(len(page_stats)), key=lambda i: page_stats[i]["characters"], reverse=True)
    selected = sorted(set([0, 1, len(pngs) // 2, len(pngs) - 1, *density_order[:2]]))
    make_contact_sheet(pngs, qa_dir / f"{directory.name}_all.png", columns=6, thumb_width=180)
    make_contact_sheet(pngs, qa_dir / f"{directory.name}_representative.png",
                       columns=2, thumb_width=600, selected=selected)
    return {
        "document": directory.name,
        "pages": len(pngs),
        "png_size": expected_size,
        "anomalies": anomalies,
        "representative_pages": [index + 1 for index in selected],
        "max_character_page": density_order[0] + 1 if density_order else None,
        "page_stats": page_stats,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("render_root", type=Path)
    parser.add_argument("qa_dir", type=Path)
    args = parser.parse_args()
    args.qa_dir.mkdir(parents=True, exist_ok=True)
    qa_resolved = args.qa_dir.resolve()
    reports = [inspect_document(directory, args.qa_dir)
               for directory in sorted(args.render_root.iterdir())
               if directory.is_dir() and directory.resolve() != qa_resolved]
    report_path = args.qa_dir / "qa_report.json"
    report_path.write_text(json.dumps(reports, ensure_ascii=False, indent=2), encoding="utf-8")
    print(report_path)
    for report in reports:
        print(f"{report['document']}: {report.get('pages', 0)} pages, "
              f"{len(report.get('anomalies', []))} anomalies, "
              f"representative={report.get('representative_pages', [])}")
    return 1 if any(report.get("error") for report in reports) else 0


if __name__ == "__main__":
    raise SystemExit(main())
