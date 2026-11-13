#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Text.hpp>

#include <catch2/catch_test_macros.hpp>

#include <GraphicsUtil.hpp>
#include <WindowUtil.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

// findCharacterPos is exercised intentionally to verify caret positions at
// the fallback boundary even though it is deprecated in favor of getShapedGlyphs
#ifdef _MSC_VER
#pragma warning(disable : 4996)
#else
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace
{
// Fonts used by the fallback tests. Every fallback fixture is derived from
// tuffy (Latin only) plus a handful of glyphs from a second script, so the
// primary font and the fallback fonts share the same global metrics.
const std::string FONT_PRIMARY     = "tuffy.ttf";
const std::string FONT_GREEK       = "tuffy-fallback-greek.ttf";
const std::string FONT_CYRILLIC    = "tuffy-fallback-cyrillic.ttf";
const std::string FONT_HEBREW      = "tuffy-fallback-hebrew.ttf";
const std::string FONT_COMBINING   = "tuffy-fallback-combining.ttf";
const std::string FONT_EMOJI       = "tuffy-fallback-emoji.ttf";
const std::string FONT_UNCOVERED   = "tuffy-fallback-uncovered.ttf";

// Count how many shaped glyphs were sourced from a given font family.
std::size_t countGlyphsForFamily(const sf::Text& text, const std::string& family)
{
    std::size_t count = 0;

    for (const sf::Text::ShapedGlyph& glyph : text.getShapedGlyphs())
    {
        if (glyph.font->getInfo().family == family)
            ++count;
    }

    return count;
}

// True when every shaped glyph of the text uses the given font.
bool allGlyphsUseFont(const sf::Text& text, const sf::Font& font)
{
    for (const sf::Text::ShapedGlyph& glyph : text.getShapedGlyphs())
    {
        if (glyph.font != &font)
            return false;
    }

    return true;
}
} // namespace

TEST_CASE("[Graphics] sf::Text font fallback", runDisplayTests())
{
    const sf::Font primary(FONT_PRIMARY);
    const sf::Font greek(FONT_GREEK);
    const sf::Font cyrillic(FONT_CYRILLIC);
    const sf::Font hebrew(FONT_HEBREW);
    const sf::Font combining(FONT_COMBINING);
    const sf::Font emoji(FONT_EMOJI);
    const sf::Font uncovered(FONT_UNCOVERED);

    SECTION("Fallback font list management")
    {
        sf::Text text(primary);

        SECTION("Starts empty")
        {
            CHECK(text.getFallbackFonts().empty());
        }

        SECTION("Adds fonts in order")
        {
            text.addFallbackFont(greek);
            text.addFallbackFont(hebrew);
            REQUIRE(text.getFallbackFonts().size() == 2);
            CHECK(text.getFallbackFonts()[0] == &greek);
            CHECK(text.getFallbackFonts()[1] == &hebrew);
        }

        SECTION("Ignores the primary font, duplicates and empty fonts")
        {
            text.addFallbackFont(primary);
            CHECK(text.getFallbackFonts().empty());

            text.addFallbackFont(greek);
            text.addFallbackFont(greek);
            CHECK(text.getFallbackFonts().size() == 1);

            // A default-constructed font has no font handle and can never
            // provide glyphs, so it must not enter the (acyclic) list.
            const sf::Font emptyFont;
            text.addFallbackFont(emptyFont);
            CHECK(text.getFallbackFonts().size() == 1);

            // A font that failed to open is rejected the same way.
            sf::Font brokenFont;
            CHECK(!brokenFont.openFromFile("does/not/exist.ttf"));
            text.addFallbackFont(brokenFont);
            CHECK(text.getFallbackFonts().size() == 1);
        }

        SECTION("Removes a font")
        {
            text.addFallbackFont(greek);
            text.addFallbackFont(hebrew);
            text.removeFallbackFont(greek);
            REQUIRE(text.getFallbackFonts().size() == 1);
            CHECK(text.getFallbackFonts()[0] == &hebrew);

            // Removing an absent font is a no-op.
            text.removeFallbackFont(cyrillic);
            CHECK(text.getFallbackFonts().size() == 1);
        }

        SECTION("Clears all fonts")
        {
            text.addFallbackFont(greek);
            text.addFallbackFont(hebrew);
            text.clearFallbackFonts();
            CHECK(text.getFallbackFonts().empty());
        }
    }

    SECTION("Selects the first covering font per code point")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);
        text.addFallbackFont(cyrillic);

        sf::String string;
        string += U'A'; // Latin: primary
        string += U'Β'; // Greek capital beta: greek fixture
        string += U'Б'; // Cyrillic capital be: cyrillic fixture
        string += U' '; // space: primary
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 4);
        CHECK(glyphs[0].font == &primary);
        CHECK(glyphs[1].font == &greek);
        CHECK(glyphs[2].font == &cyrillic);
        CHECK(glyphs[3].font == &primary);
    }

    SECTION("Consultation order is deterministic")
    {
        // Two independently opened instances of the same Greek font both cover
        // the code point; whichever instance is listed first must win.
        const sf::Font greekFirst(FONT_GREEK);
        const sf::Font greekSecond(FONT_GREEK);
        sf::String greekString;
        greekString += U'α';

        sf::Text firstWins(primary);
        firstWins.addFallbackFont(greekFirst);
        firstWins.addFallbackFont(greekSecond);
        firstWins.setString(greekString);
        REQUIRE(firstWins.getShapedGlyphs().size() == 1);
        CHECK(firstWins.getShapedGlyphs()[0].font == &greekFirst);

        sf::Text secondWins(primary);
        secondWins.addFallbackFont(greekSecond);
        secondWins.addFallbackFont(greekFirst);
        secondWins.setString(greekString);
        REQUIRE(secondWins.getShapedGlyphs().size() == 1);
        CHECK(secondWins.getShapedGlyphs()[0].font == &greekSecond);

        // A font that does not cover the code point is skipped in favor of a
        // later one that does.
        sf::Text skipsUncovering(primary);
        skipsUncovering.addFallbackFont(cyrillic);
        skipsUncovering.addFallbackFont(greek);
        skipsUncovering.setString(greekString);
        REQUIRE(skipsUncovering.getShapedGlyphs().size() == 1);
        CHECK(skipsUncovering.getShapedGlyphs()[0].font == &greek);
    }

    SECTION("Multiple scripts in one line")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);
        text.addFallbackFont(cyrillic);
        text.addFallbackFont(hebrew);

        sf::String string;
        string += U'A';
        string += U'α'; // Greek
        string += U'а'; // Cyrillic
        string += U'א'; // Hebrew
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 4);
        CHECK(glyphs[0].font == &primary);
        CHECK(glyphs[1].font == &greek);
        CHECK(glyphs[2].font == &cyrillic);
        CHECK(glyphs[3].font == &hebrew);

        // Every glyph is positioned and contributes a positive advance.
        for (const auto& glyph : glyphs)
            CHECK(glyph.glyph.advance > 0.0f);
    }

    SECTION("Emoji and symbols use the fallback font")
    {
        sf::Text text(primary);
        text.addFallbackFont(emoji);

        sf::String string;
        string += U'☺';  // U+263A, BMP
        string += U'😀'; // U+1F600, supplementary plane
        string += U'😃'; // U+1F603, supplementary plane
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 3);
        CHECK(std::all_of(glyphs.begin(),
                          glyphs.end(),
                          [&](const sf::Text::ShapedGlyph& glyph) { return glyph.font == &emoji; }));

        // The supplementary-plane glyphs must be real rasterized glyphs from
        // the format 12 cmap subtable rather than the primary .notdef box.
        for (const auto& glyph : glyphs)
        {
            CHECK(glyph.glyph.textureRect.size.x > 0);
            CHECK(glyph.glyph.textureRect.size.y > 0);
        }
    }

    SECTION("Combining marks stay with their base cluster")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);
        text.addFallbackFont(combining);

        SECTION("Base from primary, mark from fallback")
        {
            sf::String string;
            string += U'e';
            string += U'́'; // combining acute, only in the combining fixture
            text.setString(string);

            const auto& glyphs = text.getShapedGlyphs();
            // The cluster is never split across fonts: the base decides.
            CHECK_FALSE(glyphs.empty());
            CHECK(allGlyphsUseFont(text, primary));
            // Both code points map to the same input cluster.
            for (const auto& glyph : glyphs)
                CHECK(glyph.cluster == 0);
        }

        SECTION("Base from fallback, mark from a different fallback")
        {
            sf::String string;
            string += U'α'; // greek fixture
            string += U'́'; // combining fixture
            text.setString(string);

            const auto& glyphs = text.getShapedGlyphs();
            CHECK_FALSE(glyphs.empty());
            // The whole cluster is shaped by the font of its first code point.
            CHECK(allGlyphsUseFont(text, greek));
        }

        SECTION("Standalone combining mark resolves to its own font")
        {
            sf::String string;
            string += U'́';
            text.setString(string);
            const auto& glyphs = text.getShapedGlyphs();
            REQUIRE(glyphs.size() == 1);
            CHECK(glyphs[0].font == &combining);
        }
    }

    SECTION("Right-to-left runs keep visual order and font assignment")
    {
        sf::Text text(primary);
        text.addFallbackFont(hebrew);

        sf::String string;
        string += U'A';
        string += U'א'; // alef
        string += U'ב'; // bet
        string += U'B';
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 4);

        // After the final cluster sort, entries are in logical (cluster) order.
        for (std::size_t i = 1; i < glyphs.size(); ++i)
            CHECK(glyphs[i - 1].cluster <= glyphs[i].cluster);

        CHECK(glyphs[0].font == &primary);
        CHECK(glyphs[1].font == &hebrew);
        CHECK(glyphs[2].font == &hebrew);
        CHECK(glyphs[3].font == &primary);

        // The Hebrew glyphs are RTL, the Latin glyphs LTR.
        CHECK(glyphs[1].textDirection == sf::Text::TextDirection::RightToLeft);
        CHECK(glyphs[2].textDirection == sf::Text::TextDirection::RightToLeft);
        CHECK(glyphs[0].textDirection == sf::Text::TextDirection::LeftToRight);
        CHECK(glyphs[3].textDirection == sf::Text::TextDirection::LeftToRight);

        // Visually the two Hebrew glyphs appear in reverse logical order, so
        // bet (cluster 2) is positioned to the left of alef (cluster 1).
        const auto& alef = glyphs[1];
        const auto& bet  = glyphs[2];
        CHECK(bet.position.x < alef.position.x);
    }

    SECTION("Deterministic degradation when every font lacks a glyph")
    {
        sf::Text text(primary);
        text.addFallbackFont(uncovered);

        sf::String string;
        string += U'ह'; // Devanagari, in neither font
        string += U'中'; // CJK, in neither font
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 2);
        // The primary font always wins the fallback, rendering its own
        // missing-glyph representation instead of crashing or skipping.
        CHECK(allGlyphsUseFont(text, primary));
    }

    SECTION("Control characters and newlines keep deterministic handling")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);

        sf::String string;
        string += U'A';
        string += char32_t(0x01); // control character: skipped
        string += U'Β';           // Greek
        string += U'\n';
        string += U'α';
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        // Control character contributes nothing; the newline is its own
        // cluster entry, leaving clusters 0, 2, 3 (newline) and 4.
        std::vector<std::uint32_t> clusters;
        for (const auto& glyph : glyphs)
            clusters.push_back(glyph.cluster);
        CHECK(clusters == std::vector<std::uint32_t>{0, 2, 3, 4});

        // The newline entry still belongs to the primary font.
        const auto newline = std::find_if(glyphs.begin(),
                                          glyphs.end(),
                                          [](const sf::Text::ShapedGlyph& glyph) { return glyph.cluster == 3; });
        REQUIRE(newline != glyphs.end());
        CHECK(newline->font == &primary);
    }

    SECTION("Bold and outline stay consistent across the fallback boundary")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);
        text.setStyle(sf::Text::Bold | sf::Text::Italic);
        text.setOutlineThickness(2.0f);

        sf::String string;
        string += U'A';
        string += U'α';
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 2);
        CHECK(glyphs[0].font == &primary);
        CHECK(glyphs[1].font == &greek);

        // All fill glyphs are rasterized at the same character size, so the
        // bold glyph metrics stay comparable across the font boundary.
        for (const auto& glyph : glyphs)
        {
            CHECK(glyph.glyph.textureRect.size.x > 0);
            CHECK(glyph.glyph.textureRect.size.y > 0);
            CHECK(glyph.glyph.advance > 0.0f);
        }

        // Outline and fill geometry are generated for both font runs.
        CHECK(text.getOutlineVertexData().getVertexCount() > 0);
        CHECK(text.getVertexData().getVertexCount() > 0);
    }

    SECTION("Caret positions are continuous across the fallback boundary")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);

        sf::String string;
        string += U'A';
        string += U'Β';
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 2);

        // The second glyph starts where the first glyph's advance ends, so the
        // caret does not jump or overlap at the font-switch boundary.
        const float firstAdvance = glyphs[0].position.x + glyphs[0].glyph.advance;
        CHECK(std::abs(glyphs[1].position.x - firstAdvance) <= 0.6f);

        // findCharacterPos stays consistent with the shaped geometry.
        const sf::Vector2f caret0 = text.findCharacterPos(0);
        const sf::Vector2f caret1 = text.findCharacterPos(1);
        CHECK(caret1.x > caret0.x);
    }

    SECTION("Repeated queries do not rebuild the glyph textures")
    {
        sf::Text text(primary);
        text.addFallbackFont(greek);

        sf::String string;
        string += U'A';
        string += U'Β';
        text.setString(string);

        (void)text.getShapedGlyphs();
        const std::uint64_t primaryHandle = primary.getTexture(text.getCharacterSize()).getNativeHandle();
        const std::uint64_t greekHandle   = greek.getTexture(text.getCharacterSize()).getNativeHandle();
        const std::size_t   vertexCount   = text.getVertexData().getVertexCount();

        // Several geometry queries later the textures and geometry are stable;
        // fallback lookups are served from the coverage cache, not rebuilt.
        for (int i = 0; i < 5; ++i)
            (void)text.getShapedGlyphs();

        CHECK(primary.getTexture(text.getCharacterSize()).getNativeHandle() == primaryHandle);
        CHECK(greek.getTexture(text.getCharacterSize()).getNativeHandle() == greekHandle);
        CHECK(text.getVertexData().getVertexCount() == vertexCount);
    }

    SECTION("Changing the configuration invalidates cached resolution")
    {
        sf::Text text(primary);
        text.addFallbackFont(uncovered);

        sf::String greekString;
        greekString += U'α';
        text.setString(greekString);
        REQUIRE(allGlyphsUseFont(text, primary)); // no covering font yet

        text.addFallbackFont(greek);
        REQUIRE(text.getShapedGlyphs().size() == 1);
        CHECK(text.getShapedGlyphs()[0].font == &greek);

        text.removeFallbackFont(greek);
        CHECK(allGlyphsUseFont(text, primary));
    }

    SECTION("Renders mixed-font text to a render target")
    {
        // Construct the GL context before the fonts so their glyph page
        // textures are created inside an active context. The outer fonts may
        // already have lazily created textures without one in earlier sections,
        // so this section uses its own font instances.
        sf::RenderTexture renderTexture({256, 64});

        // Use fonts local to this section so their glyph pages are only ever
        // touched while the render texture's context is current.
        const sf::Font renderPrimary(FONT_PRIMARY);
        const sf::Font renderGreek(FONT_GREEK);

        sf::String mixed;
        mixed += U'A';
        mixed += U'α';
        sf::Text text(renderPrimary, mixed, 30);
        text.addFallbackFont(renderGreek);
        text.setPosition({8.0f, 8.0f});

        CHECK_NOTHROW(renderTexture.draw(text));
        CHECK_NOTHROW(renderTexture.display());

        const sf::Image first = renderTexture.getTexture().copyToImage();

        // Both the Latin run (primary texture) and the Greek run (fallback
        // texture) must have produced visible pixels, proving both glyph
        // textures were sampled through the per-font draw ranges.
        std::size_t opaquePixels = 0;
        for (unsigned int y = 0; y < first.getSize().y; ++y)
            for (unsigned int x = 0; x < first.getSize().x; ++x)
                if (first.getPixel({x, y}).a != 0)
                    ++opaquePixels;
        // Two 30px glyphs comfortably produce more than a few hundred covered pixels.
        CHECK(opaquePixels > 100);

        // Rendering again is deterministic and yields identical pixels. Clear
        // with the same transparent background the texture started with.
        renderTexture.clear(sf::Color::Transparent);
        renderTexture.draw(text);
        renderTexture.display();
        const sf::Image second = renderTexture.getTexture().copyToImage();
        REQUIRE(second.getSize() == first.getSize());
        bool identical = true;
        for (unsigned int y = 0; y < first.getSize().y && identical; ++y)
            for (unsigned int x = 0; x < first.getSize().x; ++x)
                if (second.getPixel({x, y}) != first.getPixel({x, y}))
                {
                    identical = false;
                    break;
                }
        CHECK(identical);
    }

    SECTION("No fallback configured preserves the single-font path")
    {
        sf::Text text(primary);
        sf::String string;
        string += U'A';
        string += U'Β'; // Greek, missing in primary
        text.setString(string);

        const auto& glyphs = text.getShapedGlyphs();
        REQUIRE(glyphs.size() == 2);
        CHECK(allGlyphsUseFont(text, primary));
        CHECK(countGlyphsForFamily(text, "Tuffy") == 2);
    }
}
