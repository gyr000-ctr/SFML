#include <SFML/Audio/InputSoundFile.hpp>

// Other 1st party headers
#include <SFML/System/Exception.hpp>
#include <SFML/System/FileInputStream.hpp>
#include <SFML/System/Time.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <SystemUtil.hpp>
#include <algorithm>
#include <array>
#include <type_traits>
#include <vector>

TEST_CASE("[Audio] sf::InputSoundFile")
{
    SECTION("Type traits")
    {
        STATIC_CHECK(!std::is_copy_constructible_v<sf::InputSoundFile>);
        STATIC_CHECK(!std::is_copy_assignable_v<sf::InputSoundFile>);
        STATIC_CHECK(std::is_nothrow_move_constructible_v<sf::InputSoundFile>);
        STATIC_CHECK(std::is_nothrow_move_assignable_v<sf::InputSoundFile>);
    }

    SECTION("Construction")
    {
        SECTION("Default constructor")
        {
            const sf::InputSoundFile inputSoundFile;
            CHECK(inputSoundFile.getSampleCount() == 0);
            CHECK(inputSoundFile.getChannelCount() == 0);
            CHECK(inputSoundFile.getSampleRate() == 0);
            CHECK(inputSoundFile.getDuration() == sf::Time::Zero);
            CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
            CHECK(inputSoundFile.getSampleOffset() == 0);
        }

        SECTION("Invalid file")
        {
            CHECK_THROWS_AS(sf::InputSoundFile("does/not/exist.wav"), sf::Exception);
        }

        SECTION("Valid file")
        {
            SECTION("flac")
            {
                const sf::InputSoundFile inputSoundFile("ding.flac");
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("mp3")
            {
                const sf::InputSoundFile inputSoundFile("ding.mp3");
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("ogg")
            {
                const sf::InputSoundFile inputSoundFile("doodle_pop.ogg");
                CHECK(inputSoundFile.getSampleCount() == 2'116'992);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(24'002'176));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

#ifdef SFML_ENABLE_OPUS_TESTS
            SECTION("opus mono")
            {
                const sf::InputSoundFile inputSoundFile("opus_mono_48k.opus");
                CHECK(inputSoundFile.getSampleCount() == 48'000);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 48'000);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("opus stereo")
            {
                const sf::InputSoundFile inputSoundFile("opus_stereo_48k.opus");
                CHECK(inputSoundFile.getSampleCount() == 96'000);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 48'000);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("opus 44.1 kHz source")
            {
                // Opus always decodes at 48 kHz, regardless of the sample rate of the encoded source
                const sf::InputSoundFile inputSoundFile("opus_mono_44k.opus");
                CHECK(inputSoundFile.getSampleCount() == 48'000);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 48'000);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
            }

            SECTION("opus 16 kHz source")
            {
                const sf::InputSoundFile inputSoundFile("opus_mono_16k.opus");
                CHECK(inputSoundFile.getSampleCount() == 48'000);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 48'000);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
            }
#endif

            SECTION("wav")
            {
                const sf::InputSoundFile inputSoundFile("killdeer.wav");
                CHECK(inputSoundFile.getSampleCount() == 112'941);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 22'050);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(5'122'040));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }
        }

        SECTION("Memory")
        {
            const auto               memory = loadIntoMemory("killdeer.wav");
            const sf::InputSoundFile inputSoundFile(memory.data(), memory.size());
            CHECK(inputSoundFile.getSampleCount() == 112'941);
            CHECK(inputSoundFile.getChannelCount() == 1);
            CHECK(inputSoundFile.getSampleRate() == 22'050);
            CHECK(inputSoundFile.getDuration() == sf::microseconds(5'122'040));
            CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
            CHECK(inputSoundFile.getSampleOffset() == 0);
        }

        SECTION("Stream")
        {
            SECTION("flac")
            {
                sf::FileInputStream      stream("ding.flac");
                const sf::InputSoundFile inputSoundFile(stream);
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("mp3")
            {
                sf::FileInputStream      stream("ding.mp3");
                const sf::InputSoundFile inputSoundFile(stream);
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("ogg")
            {
                sf::FileInputStream      stream("doodle_pop.ogg");
                const sf::InputSoundFile inputSoundFile(stream);
                CHECK(inputSoundFile.getSampleCount() == 2'116'992);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(24'002'176));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("wav")
            {
                sf::FileInputStream      stream("killdeer.wav");
                const sf::InputSoundFile inputSoundFile(stream);
                CHECK(inputSoundFile.getSampleCount() == 112'941);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 22'050);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(5'122'040));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }
        }
    }

    SECTION("openFromFile()")
    {
        sf::InputSoundFile inputSoundFile;

        SECTION("Invalid file")
        {
            CHECK(!inputSoundFile.openFromFile("does/not/exist.wav"));
        }

        SECTION("Valid file")
        {
            const std::u32string filenameSuffix = GENERATE(U"", U"-ń", U"-🐌");

            SECTION("flac")
            {
                const std::filesystem::path filename = U"ding" + filenameSuffix + U".flac";
                INFO("Filename: " << reinterpret_cast<const char*>(filename.u8string().c_str()));

                REQUIRE(inputSoundFile.openFromFile(filename));
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("mp3")
            {
                const std::filesystem::path filename = U"ding" + filenameSuffix + U".mp3";
                INFO("Filename: " << reinterpret_cast<const char*>(filename.u8string().c_str()));

                REQUIRE(inputSoundFile.openFromFile(filename));
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("ogg")
            {
                const std::filesystem::path filename = U"doodle_pop" + filenameSuffix + U".ogg";
                INFO("Filename: " << reinterpret_cast<const char*>(filename.u8string().c_str()));

                REQUIRE(inputSoundFile.openFromFile(filename));
                CHECK(inputSoundFile.getSampleCount() == 2'116'992);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(24'002'176));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

#ifdef SFML_ENABLE_OPUS_TESTS
            SECTION("opus")
            {
                REQUIRE(inputSoundFile.openFromFile("opus_stereo_48k.opus"));
                CHECK(inputSoundFile.getSampleCount() == 96'000);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 48'000);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }
#endif

            SECTION("wav")
            {
                const std::filesystem::path filename = U"killdeer" + filenameSuffix + U".wav";
                INFO("Filename: " << reinterpret_cast<const char*>(filename.u8string().c_str()));

                REQUIRE(inputSoundFile.openFromFile(filename));
                CHECK(inputSoundFile.getSampleCount() == 112'941);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 22'050);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(5'122'040));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }
        }
    }

    SECTION("openFromMemory()")
    {
        SECTION("wav")
        {
            const auto         memory = loadIntoMemory("killdeer.wav");
            sf::InputSoundFile inputSoundFile;
            REQUIRE(inputSoundFile.openFromMemory(memory.data(), memory.size()));
            CHECK(inputSoundFile.getSampleCount() == 112'941);
            CHECK(inputSoundFile.getChannelCount() == 1);
            CHECK(inputSoundFile.getSampleRate() == 22'050);
            CHECK(inputSoundFile.getDuration() == sf::microseconds(5'122'040));
            CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
            CHECK(inputSoundFile.getSampleOffset() == 0);
        }

#ifdef SFML_ENABLE_OPUS_TESTS
        SECTION("opus")
        {
            const auto         memory = loadIntoMemory("opus_mono_48k.opus");
            sf::InputSoundFile inputSoundFile;
            REQUIRE(inputSoundFile.openFromMemory(memory.data(), memory.size()));
            CHECK(inputSoundFile.getSampleCount() == 48'000);
            CHECK(inputSoundFile.getChannelCount() == 1);
            CHECK(inputSoundFile.getSampleRate() == 48'000);
            CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
            CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
            CHECK(inputSoundFile.getSampleOffset() == 0);
        }
#endif
    }

    SECTION("openFromStream()")
    {
        sf::InputSoundFile  inputSoundFile;
        sf::FileInputStream stream;

        SECTION("Invalid stream")
        {
            CHECK(!inputSoundFile.openFromStream(stream));
        }

        SECTION("Valid stream")
        {
            SECTION("flac")
            {
                REQUIRE(stream.open("ding.flac"));
                REQUIRE(inputSoundFile.openFromStream(stream));
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("mp3")
            {
                REQUIRE(stream.open("ding.mp3"));
                REQUIRE(inputSoundFile.openFromStream(stream));
                CHECK(inputSoundFile.getSampleCount() == 87'798);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

            SECTION("ogg")
            {
                REQUIRE(stream.open("doodle_pop.ogg"));
                REQUIRE(inputSoundFile.openFromStream(stream));
                CHECK(inputSoundFile.getSampleCount() == 2'116'992);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 44'100);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(24'002'176));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }

#ifdef SFML_ENABLE_OPUS_TESTS
            SECTION("opus")
            {
                REQUIRE(stream.open("opus_stereo_48k.opus"));
                REQUIRE(inputSoundFile.openFromStream(stream));
                CHECK(inputSoundFile.getSampleCount() == 96'000);
                CHECK(inputSoundFile.getChannelCount() == 2);
                CHECK(inputSoundFile.getSampleRate() == 48'000);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(1'000'000));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }
#endif

            SECTION("wav")
            {
                REQUIRE(stream.open("killdeer.wav"));
                REQUIRE(inputSoundFile.openFromStream(stream));
                CHECK(inputSoundFile.getSampleCount() == 112'941);
                CHECK(inputSoundFile.getChannelCount() == 1);
                CHECK(inputSoundFile.getSampleRate() == 22'050);
                CHECK(inputSoundFile.getDuration() == sf::microseconds(5'122'040));
                CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
                CHECK(inputSoundFile.getSampleOffset() == 0);
            }
        }
    }

    SECTION("seek(std::uint64_t)")
    {
        SECTION("flac")
        {
            sf::InputSoundFile inputSoundFile("ding.flac");
            inputSoundFile.seek(1'000);
            CHECK(inputSoundFile.getTimeOffset() == sf::microseconds(22'675));
            CHECK(inputSoundFile.getSampleOffset() == 1'000);
        }

        SECTION("mp3")
        {
            sf::InputSoundFile inputSoundFile("ding.mp3");
            inputSoundFile.seek(1'000);
            CHECK(inputSoundFile.getTimeOffset() == sf::microseconds(22'675));
            CHECK(inputSoundFile.getSampleOffset() == 1'000);
        }

        SECTION("ogg")
        {
            sf::InputSoundFile inputSoundFile("doodle_pop.ogg");
            inputSoundFile.seek(1'000);
            CHECK(inputSoundFile.getTimeOffset() == sf::microseconds(11'337));
            CHECK(inputSoundFile.getSampleOffset() == 1'000);
        }

#ifdef SFML_ENABLE_OPUS_TESTS
        SECTION("opus")
        {
            sf::InputSoundFile inputSoundFile("opus_mono_48k.opus");
            inputSoundFile.seek(1'000);
            CHECK(inputSoundFile.getTimeOffset() == sf::microseconds(20'833));
            CHECK(inputSoundFile.getSampleOffset() == 1'000);
        }
#endif

        SECTION("wav")
        {
            sf::InputSoundFile inputSoundFile("killdeer.wav");
            inputSoundFile.seek(1'000);
            CHECK(inputSoundFile.getTimeOffset() == sf::microseconds(45'351));
            CHECK(inputSoundFile.getSampleOffset() == 1'000);
        }
    }

    SECTION("seek(Time)")
    {
        sf::InputSoundFile inputSoundFile("ding.flac");
        inputSoundFile.seek(sf::milliseconds(100));
        CHECK(inputSoundFile.getSampleCount() == 87'798);
        CHECK(inputSoundFile.getChannelCount() == 1);
        CHECK(inputSoundFile.getSampleRate() == 44'100);
        CHECK(inputSoundFile.getDuration() == sf::microseconds(1'990'884));
        CHECK(inputSoundFile.getTimeOffset() == sf::milliseconds(100));
        CHECK(inputSoundFile.getSampleOffset() == 4'410);
    }

    SECTION("read()")
    {
        sf::InputSoundFile inputSoundFile("ding.flac");

        SECTION("Null address")
        {
            CHECK(inputSoundFile.read(nullptr, 10) == 0);
        }

        std::array<std::int16_t, 4> samples{};

        SECTION("Zero count")
        {
            CHECK(inputSoundFile.read(samples.data(), 0) == 0);
        }

        SECTION("Successful read")
        {
            SECTION("flac")
            {
                inputSoundFile = sf::InputSoundFile("ding.flac");
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples == std::array<std::int16_t, 4>{0, 1, -1, 4});
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples == std::array<std::int16_t, 4>{1, 4, 9, 6});
            }

            SECTION("mp3")
            {
                inputSoundFile = sf::InputSoundFile("ding.mp3");
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples == std::array<std::int16_t, 4>{0, -2, 0, 2});
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples == std::array<std::int16_t, 4>{1, 4, 6, 8});
            }

            SECTION("ogg")
            {
                inputSoundFile = sf::InputSoundFile("doodle_pop.ogg");
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples == std::array<std::int16_t, 4>{-827, -985, -1168, -1319});
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples == std::array<std::int16_t, 4>{-1738, -1883, -2358, -2497});
            }

#ifdef SFML_ENABLE_OPUS_TESTS
            SECTION("opus")
            {
                // Decoded output is compared against an opusdec reference
                // decoding with a margin for codec implementation differences
                inputSoundFile = sf::InputSoundFile("opus_mono_48k.opus");
                CHECK(inputSoundFile.read(samples.data(), samples.size()) == 4);
                CHECK(samples[0] == Catch::Approx(339).margin(32));
                CHECK(samples[1] == Catch::Approx(1178).margin(32));
                CHECK(samples[2] == Catch::Approx(2168).margin(32));
                CHECK(samples[3] == Catch::Approx(3020).margin(32));
            }
#endif

            SECTION("wav")
            {
                // Cannot be tested since reading from a .wav file triggers UB
            }
        }
    }

    SECTION("close()")
    {
        sf::InputSoundFile inputSoundFile("ding.flac");
        inputSoundFile.close();
        CHECK(inputSoundFile.getSampleCount() == 0);
        CHECK(inputSoundFile.getChannelCount() == 0);
        CHECK(inputSoundFile.getSampleRate() == 0);
        CHECK(inputSoundFile.getDuration() == sf::Time::Zero);
        CHECK(inputSoundFile.getTimeOffset() == sf::Time::Zero);
        CHECK(inputSoundFile.getSampleOffset() == 0);
    }

#ifdef SFML_ENABLE_OPUS_TESTS
    SECTION("opus seek and loop")
    {
        sf::InputSoundFile inputSoundFile("opus_stereo_48k.opus");

        // Decode the whole file in small chunks to exercise streaming reads
        std::vector<std::int16_t>  allSamples(inputSoundFile.getSampleCount());
        std::uint64_t              totalRead = 0;
        std::array<std::int16_t, 997> chunk{};
        while (const std::uint64_t count = inputSoundFile.read(chunk.data(), chunk.size()))
        {
            std::copy_n(chunk.data(), count, allSamples.begin() + static_cast<std::ptrdiff_t>(totalRead));
            totalRead += count;
        }
        CHECK(totalRead == inputSoundFile.getSampleCount());

        // End of file reached: further reads return nothing
        CHECK(inputSoundFile.read(chunk.data(), chunk.size()) == 0);

        // Seeking must land on sample-exact positions
        std::array<std::int16_t, 512> samples{};
        inputSoundFile.seek(12'344);
        CHECK(inputSoundFile.getSampleOffset() == 12'344);
        CHECK(inputSoundFile.read(samples.data(), samples.size()) == samples.size());
        CHECK(std::equal(samples.begin(), samples.end(), allSamples.begin() + 12'344));

        // Seeking near the end of the file
        inputSoundFile.seek(inputSoundFile.getSampleCount() - 100);
        CHECK(inputSoundFile.read(samples.data(), samples.size()) == 100);
        CHECK(std::equal(samples.begin(), samples.begin() + 100, allSamples.end() - 100));

        // Seeking beyond the end of the file jumps to the end
        inputSoundFile.seek(inputSoundFile.getSampleCount() + 1'000);
        CHECK(inputSoundFile.read(samples.data(), samples.size()) == 0);

        // Seeking back to the beginning (as done when looping) restarts playback exactly
        inputSoundFile.seek(0);
        CHECK(inputSoundFile.getSampleOffset() == 0);
        CHECK(inputSoundFile.read(samples.data(), samples.size()) == samples.size());
        CHECK(std::equal(samples.begin(), samples.end(), allSamples.begin()));
    }

    SECTION("opus long file seek")
    {
        sf::InputSoundFile inputSoundFile("opus_long_mono.opus");
        REQUIRE(inputSoundFile.getSampleCount() == 1'440'000);
        REQUIRE(inputSoundFile.getChannelCount() == 1);
        REQUIRE(inputSoundFile.getSampleRate() == 48'000);
        CHECK(inputSoundFile.getDuration() == sf::microseconds(30'000'000));

        // Decode the whole file as a reference
        std::vector<std::int16_t>   allSamples(inputSoundFile.getSampleCount());
        std::uint64_t               totalRead = 0;
        std::array<std::int16_t, 8192> chunk{};
        while (const std::uint64_t count = inputSoundFile.read(chunk.data(), chunk.size()))
        {
            std::copy_n(chunk.data(), count, allSamples.begin() + static_cast<std::ptrdiff_t>(totalRead));
            totalRead += count;
        }
        REQUIRE(totalRead == inputSoundFile.getSampleCount());

        // Seeking across the whole file must produce sample-exact positions,
        // including when jumping backwards
        std::array<std::int16_t, 512> samples{};
        for (const std::uint64_t offset : {1ULL, 61'723ULL, 480'000ULL, 1'000'000ULL, 1'439'000ULL, 100ULL, 1'439'000ULL})
        {
            INFO("Seek offset: " << offset);
            inputSoundFile.seek(offset);
            CHECK(inputSoundFile.getSampleOffset() == offset);
            CHECK(inputSoundFile.read(samples.data(), samples.size()) == samples.size());
            CHECK(std::equal(samples.begin(), samples.end(), allSamples.begin() + static_cast<std::ptrdiff_t>(offset)));
        }
    }

    SECTION("opus invalid files")
    {
        sf::InputSoundFile inputSoundFile;
        CHECK(!inputSoundFile.openFromFile("opus_head_only.opus"));         // missing OpusTags header
        CHECK(!inputSoundFile.openFromFile("opus_bad_tags.opus"));          // corrupted OpusTags magic
        CHECK(!inputSoundFile.openFromFile("opus_bad_head_magic.opus"));    // corrupted OpusHead magic
        CHECK(!inputSoundFile.openFromFile("opus_bad_head_channels.opus")); // unsupported channel count
        CHECK(!inputSoundFile.openFromFile("opus_bad_head_family.opus"));   // reserved channel mapping family
        CHECK(!inputSoundFile.openFromFile("opus_missing_mapping.opus"));   // missing channel mapping table
    }

    SECTION("opus truncated file")
    {
        // A file cut in the middle of an audio page opens with a shorter duration
        sf::InputSoundFile inputSoundFile;
        REQUIRE(inputSoundFile.openFromFile("opus_truncated.opus"));
        CHECK(inputSoundFile.getChannelCount() == 2);
        CHECK(inputSoundFile.getSampleRate() == 48'000);
        CHECK(inputSoundFile.getSampleCount() == 95'376);
        CHECK(inputSoundFile.getDuration() == sf::microseconds(993'500));

        // The whole remaining file can be read without errors
        std::uint64_t               totalRead = 0;
        std::array<std::int16_t, 4096> chunk{};
        while (const std::uint64_t count = inputSoundFile.read(chunk.data(), chunk.size()))
            totalRead += count;
        CHECK(totalRead == inputSoundFile.getSampleCount());
    }
#else
    SECTION("opus files are rejected when Opus support is disabled")
    {
        sf::InputSoundFile inputSoundFile;
        CHECK(!inputSoundFile.openFromFile("opus_mono_48k.opus"));
        CHECK(!inputSoundFile.openFromFile("opus_stereo_48k.opus"));
    }
#endif
}
