"""Fetch real Valve Dota 2 web art and prepare small H1 RGB565 sprites.

Requires Pillow. Downloads are data only; no Steam or APK executable is run.
Original PNG files, generated art and provenance stay under private/ by default.
Hero art uses Valve's genuine transparent 32px map icons. It is not animation.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
from pathlib import Path
import urllib.request

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
CDN = "https://cdn.akamai.steamstatic.com/apps/dota2/images/dota_react/"
HEROES = [
    ("axe", "Axe", ["axe_berserkers_call", "axe_battle_hunger", "axe_counter_helix", "axe_culling_blade"]),
    ("lina", "Lina", ["lina_dragon_slave", "lina_light_strike_array", "lina_fiery_soul", "lina_laguna_blade"]),
    ("juggernaut", "Juggernaut", ["juggernaut_blade_fury", "juggernaut_healing_ward", "juggernaut_blade_dance", "juggernaut_omni_slash"]),
    ("crystal_maiden", "Crystal Maiden", ["crystal_maiden_crystal_nova", "crystal_maiden_frostbite", "crystal_maiden_brilliance_aura", "crystal_maiden_freezing_field"]),
]
ITEMS = [
    ("boots", "Boots of Speed"),
    ("blades_of_attack", "Blades of Attack"),
    ("chainmail", "Chainmail"),
    ("vitality_booster", "Vitality Booster"),
    ("energy_booster", "Energy Booster"),
    ("flask", "Healing Salve"),
]


def jobs() -> list[dict]:
    result = []
    for hero_id, (slug, label, abilities) in enumerate(HEROES):
        result.append({"kind": "hero", "hero": hero_id, "slot": None, "id": hero_id, "name": label,
                       "symbol": "dota_art_hero_" + slug, "relative_url": "heroes/icons/" + slug + ".png", "target_size": [32, 32]})
        for slot, ability in enumerate(abilities):
            result.append({"kind": "ability", "hero": hero_id, "slot": slot, "id": hero_id * 4 + slot, "name": ability,
                           "symbol": "dota_art_ability_" + ability, "relative_url": "abilities/" + ability + ".png", "target_size": [20, 20]})
    for item_id, (slug, label) in enumerate(ITEMS):
        result.append({"kind": "item", "hero": None, "slot": None, "id": item_id, "name": label,
                       "symbol": "dota_art_item_" + slug, "relative_url": "items/" + slug + ".png", "target_size": [20, 20]})
    return result


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def fetch(job: dict, cache: Path, offline: bool) -> dict:
    path = cache / job["relative_url"]
    url = CDN + job["relative_url"]
    if not path.is_file():
        if offline:
            raise FileNotFoundError(f"Missing cached original: {path}")
        request = urllib.request.Request(url, headers={"User-Agent": "H1-Dota2-AssetPreparation/1.0"})
        with urllib.request.urlopen(request, timeout=25) as response:
            data = response.read(4 * 1024 * 1024 + 1)
        if len(data) > 4 * 1024 * 1024:
            raise ValueError(f"Unexpectedly large image: {url}")
        # Validate as an image before preserving the download.
        from io import BytesIO
        with Image.open(BytesIO(data)) as image:
            image.verify()
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    data = path.read_bytes()
    with Image.open(path) as original:
        original.load()
        dimensions = list(original.size)
        source_mode = original.mode
        rgba = original.convert("RGBA")
    target = tuple(job["target_size"])
    if job["kind"] == "hero":
        if rgba.size != target:
            rgba = rgba.resize(target, Image.Resampling.LANCZOS)
        transform = "Valve transparent map icon, 32x32; no invented pose or animation."
    elif job["kind"] == "item":
        # Preserve original item aspect ratio inside a transparent square.
        rgba.thumbnail(target, Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", target, (0, 0, 0, 0))
        canvas.alpha_composite(rgba, ((target[0] - rgba.width) // 2, (target[1] - rgba.height) // 2))
        rgba = canvas
        transform = "Aspect ratio preserved and centered in transparent 20x20 square."
    else:
        rgba = rgba.resize(target, Image.Resampling.LANCZOS)
        transform = "Resized original square ability icon to 20x20."
    rgb565 = []
    alpha = []
    pixel_data = rgba.get_flattened_data() if hasattr(rgba, "get_flattened_data") else rgba.getdata()
    for red, green, blue, opacity in pixel_data:
        rgb565.append(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3))
        alpha.append(opacity)
    source_file = path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path)
    return {**job, "source_url": url, "source_file": source_file, "source_bytes": len(data),
            "source_sha256": sha256(data), "source_dimensions": dimensions, "source_mode": source_mode,
            "transformation": transform, "rgb565": rgb565, "alpha": alpha, "image": rgba,
            "pixel_bytes": len(rgb565) * 2, "alpha_bytes": len(alpha)}


def array(name: str, ctype: str, values: list[int], hex_width: int = 0) -> str:
    lines = [f"static const {ctype} {name}[{len(values)}] = {{"]
    for start in range(0, len(values), 16):
        cells = values[start:start + 16]
        lines.append("    " + ",".join(f"0x{v:0{hex_width}x}" if hex_width else str(v) for v in cells) + ",")
    return "\n".join(lines + ["};"])


def header(records: list[dict]) -> str:
    lines = ["/* Generated by tools/prepare_assets.py from Valve's public Dota 2 CDN art.",
             " * Copyright Valve Corporation. See assets/NOTICE.md and private/reports/art-provenance.json.",
             " * Pixels: row-major straight RGB565. Alpha: one 0..255 byte per pixel.",
             " * Hero IDs: 0 Axe, 1 Lina, 2 Juggernaut, 3 Crystal Maiden.",
             " * Ability slots: 0..3, original first three abilities then ultimate.",
             " * Item IDs: 0 boots, 1 blades_of_attack, 2 chainmail, 3 vitality_booster, 4 energy_booster, 5 flask.",
             " * Hero icons are genuine 32px minimap icons; no animation frames are supplied.",
             " */", "#ifndef H1_DOTA_ART_H", "#define H1_DOTA_ART_H", "",
             "#ifndef H1_DOTA_ART_TYPE_DEFINED", "#define H1_DOTA_ART_TYPE_DEFINED", "typedef struct DotaArt {", "    unsigned short w,h;", "    const unsigned short *pixels;", "    const unsigned char *alpha;", "} DotaArt;", "#endif", ""]
    for record in records:
        symbol = record["symbol"]
        lines.append(array(symbol + "_pixels", "unsigned short", record["rgb565"], 4))
        lines.append(array(symbol + "_alpha", "unsigned char", record["alpha"]))
        w, h = record["target_size"]
        lines.append(f"static const DotaArt {symbol} = {{{w},{h},{symbol}_pixels,{symbol}_alpha}};")
    for kind, function, count in (("hero", "hero", 4), ("ability", "ability", 16), ("item", "item", len(ITEMS))):
        ordered = sorted((r for r in records if r["kind"] == kind), key=lambda r: r["id"])
        lines.append(f"static const DotaArt *const dota_art_{kind}_table[{count}] = {{")
        lines.append("    " + ",".join("&" + r["symbol"] for r in ordered))
        lines.append("};")
    lines.extend(["", "static const DotaArt *dota_art_hero(int hero) {", "    return (unsigned)hero < 4u ? dota_art_hero_table[hero] : (const DotaArt *)0;", "}",
                  "static const DotaArt *dota_art_ability(int hero, int slot) {", "    return (unsigned)hero < 4u && (unsigned)slot < 4u ? dota_art_ability_table[hero*4+slot] : (const DotaArt *)0;", "}",
                  "static const DotaArt *dota_art_item(int id) {", f"    return (unsigned)id < {len(ITEMS)}u ? dota_art_item_table[id] : (const DotaArt *)0;", "}", "", "#endif", ""])
    return "\n".join(lines)


def preview(records: list[dict], target: Path) -> None:
    sheet = Image.new("RGB", (480, 320), "#112025")
    draw = ImageDraw.Draw(sheet)
    for hero_id, (_, label, _) in enumerate(HEROES):
        y = 12 + hero_id * 62
        draw.text((12, y), label, fill="white")
        hero = next(r for r in records if r["kind"] == "hero" and r["id"] == hero_id)
        sheet.paste(hero["image"], (150, y - 4), hero["image"])
        for slot in range(4):
            art = next(r for r in records if r["kind"] == "ability" and r["hero"] == hero_id and r["slot"] == slot)
            sheet.paste(art["image"], (212 + slot * 52, y), art["image"])
            draw.text((216 + slot * 52, y + 25), str(slot + 1), fill="#91a8ad")
    draw.text((12, 274), "Items 0..5", fill="white")
    for item_id in range(len(ITEMS)):
        art = next(r for r in records if r["kind"] == "item" and r["id"] == item_id)
        sheet.paste(art["image"], (150 + item_id * 48, 270), art["image"])
        draw.text((156 + item_id * 48, 296), str(item_id), fill="#91a8ad")
    sheet.save(target)


def prepare_icon(records: list[dict], target: Path, steam_logo: Path | None) -> dict:
    if steam_logo is None:
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                steam = Path(winreg.QueryValueEx(key, "SteamPath")[0])
            candidate = steam / "appcache/librarycache/570/logo.png"
            if candidate.is_file():
                steam_logo = candidate
        except (ImportError, OSError):
            pass
    if steam_logo is not None and steam_logo.is_file():
        original = Image.open(steam_logo).convert("RGBA")
        crop = [0, 0, original.height, original.height]
        icon = original.crop(crop).resize((128, 128), Image.Resampling.LANCZOS)
        provenance = {"source": str(steam_logo), "source_sha256": sha256(steam_logo.read_bytes()),
                      "crop": crop, "transformation": "Crop original Dota symbol from locally installed Steam logo wordmark, resize to 128x128, preserve alpha."}
    else:
        icon = Image.new("RGBA", (128, 128), (16, 29, 30, 255))
        for hero_id in range(4):
            record = next(r for r in records if r["kind"] == "hero" and r["id"] == hero_id)
            portrait = record["image"].resize((64, 64), Image.Resampling.NEAREST)
            icon.alpha_composite(portrait, ((hero_id % 2) * 64, (hero_id // 2) * 64))
        provenance = {"source": "Four cached original Valve hero minimap icons; see source URL/hash for hero records.",
                      "transformation": "2x2 composition of actual hero artwork, no generated imitation."}
    icon.save(target)
    return {**provenance, "output": str(target), "output_sha256": sha256(target.read_bytes()), "owner": "Valve Corporation"}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="Only use previously fetched original PNGs.")
    parser.add_argument("--cache", type=Path, default=ROOT / "private/assets/valve")
    parser.add_argument("--output", type=Path, default=ROOT / "private/generated/dota_art.h")
    parser.add_argument("--provenance", type=Path, default=ROOT / "private/reports/art-provenance.json")
    parser.add_argument("--steam-logo", type=Path, help="Optional local Steam Dota 2 logo.png; otherwise discover Windows Steam installation.")
    args = parser.parse_args()
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        records = list(pool.map(lambda job: fetch(job, args.cache, args.offline), jobs()))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    content = header(records).encode("utf-8")
    args.output.write_bytes(content)
    preview_path = args.output.with_name("dota-art-preview.png")
    preview(records, preview_path)
    icon = prepare_icon(records, args.output.with_name("icon.png"), args.steam_logo)
    source_records = [{k: v for k, v in record.items() if k not in ("rgb565", "alpha", "image")} for record in records]
    report = {"prepared_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "source": "Valve official public Dota 2 web CDN; fetched PNG files, never generated imitation art.",
              "copyright": "Dota 2 artwork and characters copyright Valve Corporation.",
              "transparency": "Straight RGB565 with separate 8-bit alpha (0 transparent, 255 opaque).",
              "hero_art_scope": "Original 32x32 transparent minimap icons; not full-body or animation sprites.",
              "header_sha256": sha256(content), "header_bytes": len(content),
              "raw_rgb565_alpha_bytes": sum(r["pixel_bytes"] + r["alpha_bytes"] for r in records),
              "counts": {"heroes": 4, "abilities": 16, "items": len(ITEMS)},
              "application_icon": icon,
              "assets": source_records}
    args.provenance.parent.mkdir(parents=True, exist_ok=True)
    args.provenance.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    args.provenance.with_name("icon-provenance.json").write_text(json.dumps(icon, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"header": str(args.output), "provenance": str(args.provenance), "preview": str(preview_path),
                      "counts": report["counts"], "art_bytes": report["raw_rgb565_alpha_bytes"]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
