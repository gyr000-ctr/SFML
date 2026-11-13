#!/usr/bin/env python3
"""Generate deterministic font fixtures for the sf::Text font fallback tests.

Every fixture starts from tuffy.ttf (which only contains Latin glyphs) and is
extended with a small subset of glyph outlines copied from DejaVu Sans. The
resulting fonts share tuffy's global metrics while covering a handful of code
points from a second script, which is exactly the setup needed to exercise
font fallback. Both source fonts are TrueType outline fonts on a 2048
units-per-em design grid, so copied outlines need no scaling.

Only the ``glyf``, ``hmtx`` and ``cmap`` tables are touched; every other table
(including tuffy's kerning and layout tables) is left untouched.

Re-run this script to regenerate the fixtures:

    python3 generate-fallback-fonts.py
"""

from pathlib import Path

from fontTools.subset import Subsetter, Options
from fontTools.ttLib import TTFont


HERE = Path(__file__).resolve().parent
TUFFY = HERE / "tuffy.ttf"
DEJAVU = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")

# Code points merged into each fixture on top of the glyphs tuffy already has.
FIXTURES = {
    # Greek capitals, lowercase and Greek question mark
    "tuffy-fallback-greek.ttf": [0x0391, 0x0392, 0x03B1, 0x03B2, 0x037E],
    # Cyrillic capitals and lowercase
    "tuffy-fallback-cyrillic.ttf": [0x0410, 0x0411, 0x0430, 0x0431],
    # Hebrew block letters (right-to-left script, no contextual joining needed)
    "tuffy-fallback-hebrew.ttf": [0x05D0, 0x05D1, 0x05D2],
    # Combining marks used for base + mark clusters
    "tuffy-fallback-combining.ttf": [0x0301, 0x0302],
    # Symbols and emoji (monochrome outlines in DejaVu Sans); U+2705 is not
    # covered by DejaVu Sans and is intentionally omitted.
    "tuffy-fallback-emoji.ttf": [0x263A, 0x1F600, 0x1F603],
    # Indic and CJK code points that DejaVu itself does not cover: used to
    # test the deterministic "every font is missing the glyph" degradation
    "tuffy-fallback-uncovered.ttf": [0x0939, 0x4E2D],
}


def subset_dejavu(codepoints: list[int]) -> TTFont:
    """Return a DejaVu Sans font reduced to the requested code points."""
    font = TTFont(DEJAVU)
    options = Options()
    options.glyph_names = True
    options.notdef_outline = True
    options.recalc_bounds = True
    subsetter = Subsetter(options=options)
    subsetter.populate(unicodes=codepoints)
    subsetter.subset(font)
    return font


def rename_family(font: TTFont, family: str) -> None:
    for record in font["name"].names:
        if record.nameID in (1, 4, 16):
            record.string = family.encode(record.getEncoding())
        elif record.nameID == 6:
            record.string = family.replace(" ", "-").encode(record.getEncoding())


def copy_glyphs(base: TTFont, source: TTFont, codepoints: list[int], output: Path) -> bool:
    """Copy glyphs for missing code points from source into base.

    Composite glyphs are copied together with the full closure of their
    component glyphs.
    """
    base_glyf, source_glyf = base["glyf"], source["glyf"]
    base_hmtx, source_hmtx = base["hmtx"], source["hmtx"]
    base_cmap = base.getBestCmap()

    # A subset font can contain duplicate (platform, encoding) cmap keys
    # (BMP format 4 and full-repertoire format 12), which makes getBestCmap()
    # return null. Merge every subtable instead so supplementary code points
    # are still resolvable.
    source_cmap: dict[int, str] = {}
    for subtable in source["cmap"].tables:
        source_cmap.update(subtable.cmap)

    # Tuffy's cmap only has a BMP (16-bit) subtable. Track non-BMP code points
    # separately and add a 32-bit (format 12) subtable for them afterwards.
    supplementary: dict[int, str] = {}

    def ensure_glyph(glyph_name: str) -> None:
        if glyph_name in base_glyf.glyphs:
            return

        glyph = source_glyf[glyph_name]

        if glyph.isComposite():
            for component in glyph.components:
                ensure_glyph(component.glyphName)

        order = base.getGlyphOrder()
        base.setGlyphOrder(order + [glyph_name])
        base_glyf.glyphs[glyph_name] = glyph
        base_hmtx[glyph_name] = source_hmtx[glyph_name]

    for code_point in codepoints:
        if code_point in base_cmap:
            continue

        glyph_name = source_cmap.get(code_point)
        if glyph_name is None:
            # The source font does not cover this code point either; leave it
            # out of the fixture on purpose so missing-glyph handling can be
            # tested deterministically.
            continue

        ensure_glyph(glyph_name)

        if code_point <= 0xFFFF:
            base_cmap[code_point] = glyph_name
        else:
            supplementary[code_point] = glyph_name

    if supplementary:
        from fontTools.ttLib.tables import _c_m_a_p as cmap_module

        cmap_table = base["cmap"]
        full_mappings = dict(base.getBestCmap())
        full_mappings.update(supplementary)

        # Supplementary code points need a full-repertoire format 12 subtable.
        # FreeType resolves code points through the Windows (3/1) family and
        # selects the largest subtable, so the format 12 must share that key
        # with the BMP format 4. That pairing is valid for FreeType (and is how
        # real-world fonts expose emoji) even though fontTools' writer treats a
        # duplicated (platform, encoding, language) key as an error. Relax that
        # single check while saving this fixture.
        windows_bmp = next(t for t in cmap_table.tables if t.platformID == 3 and t.platEncID == 1)
        new_subtable = cmap_module.cmap_format_12(12)
        new_subtable.platformID = 3
        new_subtable.platEncID = 1
        new_subtable.reserved = 0
        new_subtable.length = 0
        new_subtable.language = 0
        new_subtable.cmap = full_mappings
        cmap_table.tables = [windows_bmp, new_subtable]

        original_compile = cmap_module.table__c_m_a_p.compile

        def compile_without_duplicate_check(self, ttFont):
            self.tables.sort()
            num_sub_tables = len(self.tables)
            result = b""
            for subtable in self.tables:
                data = subtable.compile(ttFont)
                result += struct.pack(">HHH", subtable.platformID, subtable.platEncID, len(data) + 4 + 8 * num_sub_tables)
                result += data
            header = struct.pack(">HH", 0, num_sub_tables)
            return header + result

        import struct

        def compile_without_duplicate_check(self, ttFont):
            # Mirrors fontTools' table__c_m_a_p.compile but without the
            # (platform, encoding, language) duplicate-key check.
            self.tables.sort()
            num_sub_tables = len(self.tables)
            total_offset = 4 + 8 * num_sub_tables
            data = struct.pack(">HH", self.tableVersion, num_sub_tables)
            table_data = b""
            seen: dict[int, int] = {}
            done: dict[bytes, int] = {}
            for subtable in self.tables:
                offset = seen.get(id(subtable.cmap))
                if offset is None:
                    chunk = subtable.compile(ttFont)
                    offset = done.get(chunk)
                    if offset is None:
                        offset = seen[id(subtable.cmap)] = done[chunk] = total_offset + len(table_data)
                        table_data += chunk
                data += struct.pack(">HHL", subtable.platformID, subtable.platEncID, offset)
            return data + table_data

        cmap_module.table__c_m_a_p.compile = compile_without_duplicate_check
        try:
            base.save(output)
        finally:
            cmap_module.table__c_m_a_p.compile = original_compile
        return True

    return False


def make_fixture(output: Path, codepoints: list[int]) -> None:
    base = TTFont(TUFFY)
    extra = subset_dejavu(codepoints)
    rename_family(base, output.stem)
    already_saved = copy_glyphs(base, extra, codepoints, output)

    if not already_saved:
        base.save(output)

    verify = TTFont(output)
    # Merge all subtables for verification because getBestCmap() can return
    # null when duplicate (platform, encoding) keys are present.
    verified_cmap: dict[int, str] = {}
    for subtable in verify["cmap"].tables:
        verified_cmap.update(subtable.cmap)
    covered = [cp for cp in codepoints if cp in verified_cmap]
    print(f"wrote {output.name} ({len(verify.getGlyphOrder())} glyphs, added {len(covered)} of {len(codepoints)} code points)")


def main() -> None:
    if not DEJAVU.exists():
        raise SystemExit(f"source font not found: {DEJAVU}")

    for name, codepoints in FIXTURES.items():
        make_fixture(HERE / name, codepoints)


if __name__ == "__main__":
    main()
