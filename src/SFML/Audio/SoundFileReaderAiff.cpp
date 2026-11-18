////////////////////////////////////////////////////////////
//
// SFML - Simple and Fast Multimedia Library
// Copyright (C) 2007-2026 Laurent Gomila (laurent@sfml-dev.org)
//
// This software is provided 'as-is', without any express or implied warranty.
// In no event will the authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it freely,
// subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented;
//    you must not claim that you wrote the original software.
//    If you use this software in a product, an acknowledgment
//    in the product documentation would be appreciated but is not required.
//
// 2. Altered source versions must be plainly marked as such,
//    and must not be misrepresented as being the original software.
//
// 3. This notice may not be removed or altered from any source distribution.
//
////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Audio/SoundFileReaderAiff.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/InputStream.hpp>

#include <algorithm>
#include <array>
#include <ostream>
#include <vector>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>


namespace
{
////////////////////////////////////////////////////////////
/// \brief Enumeration of the possible outcomes of parsing an AIFF/AIFC header
///
////////////////////////////////////////////////////////////
enum class AiffParseError
{
    None,                    //!< No error, the file is a supported AIFF/AIFC file
    NotAiff,                 //!< Not an AIFF/AIFC file (bad FORM header)
    BadChunk,                //!< Corrupt chunk structure
    MissingChunk,            //!< A mandatory chunk (COMM or SSND) is missing
    TruncatedSsnd,           //!< The SSND chunk declares more data than the stream contains
    UnsupportedCompression,  //!< Unsupported AIFC compression type
    UnsupportedSampleFormat  //!< Unsupported sample size
};

////////////////////////////////////////////////////////////
/// \brief Header information extracted from an AIFF/AIFC file
///
////////////////////////////////////////////////////////////
struct AiffHeader
{
    std::uint64_t frameCount{};    //!< Number of sample frames
    std::uint64_t dataStart{};     //!< Byte offset of the first audio sample in the stream
    std::uint64_t dataSize{};      //!< Size in bytes of the audio data
    unsigned int  channelCount{};  //!< Number of channels
    unsigned int  sampleRate{};    //!< Samples per second
    unsigned int  bitsPerSample{}; //!< Bits per sample (8, 16, 24 or 32)
    bool          littleEndian{};  //!< Whether samples are stored little-endian (`sowt` compression)
};

////////////////////////////////////////////////////////////
/// \brief Result of parsing an AIFF/AIFC header
///
////////////////////////////////////////////////////////////
struct AiffParseResult
{
    AiffParseError error{AiffParseError::None};
    AiffHeader     header;
};

////////////////////////////////////////////////////////////
bool readBytes(sf::InputStream& stream, void* buffer, std::size_t size)
{
    const auto count = stream.read(buffer, size);
    return count.has_value() && *count == size;
}

////////////////////////////////////////////////////////////
bool readU16BE(sf::InputStream& stream, std::uint16_t& value)
{
    std::array<std::uint8_t, 2> bytes{};
    if (!readBytes(stream, bytes.data(), bytes.size()))
        return false;

    value = static_cast<std::uint16_t>((bytes[0] << 8u) | bytes[1]);
    return true;
}

////////////////////////////////////////////////////////////
bool readU32BE(sf::InputStream& stream, std::uint32_t& value)
{
    std::array<std::uint8_t, 4> bytes{};
    if (!readBytes(stream, bytes.data(), bytes.size()))
        return false;

    value = (static_cast<std::uint32_t>(bytes[0]) << 24u) | (static_cast<std::uint32_t>(bytes[1]) << 16u) |
            (static_cast<std::uint32_t>(bytes[2]) << 8u) | static_cast<std::uint32_t>(bytes[3]);
    return true;
}

////////////////////////////////////////////////////////////
/// \brief Convert an 80-bit IEEE 754 extended precision float to a double
///
////////////////////////////////////////////////////////////
double parseExtended80(const std::array<std::uint8_t, 10>& bytes)
{
    const auto exponent = static_cast<std::uint16_t>(((bytes[0] & 0x7Fu) << 8u) | bytes[1]);

    std::uint64_t mantissa = 0;
    for (std::size_t i = 0; i < 8; ++i)
        mantissa = (mantissa << 8u) | bytes[2 + i];

    if (exponent == 0 && mantissa == 0)
        return 0.0;

    // The mantissa has an explicit integer bit, its value is mantissa * 2^(exponent - 16383 - 63)
    const double value = std::ldexp(static_cast<double>(mantissa), static_cast<int>(exponent) - 16383 - 63);
    return (bytes[0] & 0x80u) ? -value : value;
}

////////////////////////////////////////////////////////////
bool parseCommChunk(sf::InputStream& stream, std::uint32_t chunkSize, bool aifc, AiffParseResult& result)
{
    if (chunkSize < 18)
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    std::uint16_t                channels{};
    std::uint32_t                frameCount{};
    std::uint16_t                bitsPerSample{};
    std::array<std::uint8_t, 10> sampleRateBytes{};

    if (!readU16BE(stream, channels) || !readU32BE(stream, frameCount) || !readU16BE(stream, bitsPerSample) ||
        !readBytes(stream, sampleRateBytes.data(), sampleRateBytes.size()))
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    if (channels == 0)
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    if (channels > 8)
    {
        result.error = AiffParseError::UnsupportedSampleFormat;
        return false;
    }

    if (bitsPerSample != 8 && bitsPerSample != 16 && bitsPerSample != 24 && bitsPerSample != 32)
    {
        result.error = AiffParseError::UnsupportedSampleFormat;
        return false;
    }

    const double sampleRate = parseExtended80(sampleRateBytes);
    if (sampleRate <= 0.0 || sampleRate > 4294967295.0)
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    // AIFC files carry an additional compression type (and a Pascal-style compression name we ignore)
    bool littleEndian = false;
    if (aifc)
    {
        if (chunkSize < 22)
        {
            result.error = AiffParseError::BadChunk;
            return false;
        }

        std::array<char, 4> compression{};
        if (!readBytes(stream, compression.data(), compression.size()))
        {
            result.error = AiffParseError::BadChunk;
            return false;
        }

        if (std::memcmp(compression.data(), "NONE", 4) == 0 || std::memcmp(compression.data(), "twos", 4) == 0)
        {
            littleEndian = false;
        }
        else if (std::memcmp(compression.data(), "sowt", 4) == 0)
        {
            littleEndian = true;
        }
        else
        {
            result.error = AiffParseError::UnsupportedCompression;
            return false;
        }
    }

    result.header.channelCount  = channels;
    result.header.frameCount    = frameCount;
    result.header.bitsPerSample = bitsPerSample;
    result.header.sampleRate    = static_cast<unsigned int>(sampleRate + 0.5);
    result.header.littleEndian  = littleEndian;
    return true;
}

////////////////////////////////////////////////////////////
bool parseSsndChunk(sf::InputStream& stream, std::uint32_t chunkSize, AiffParseResult& result)
{
    if (chunkSize < 8)
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    std::uint32_t offset = 0;
    std::uint32_t blockSize = 0; // Block alignment is not relevant when reading, ignore it
    if (!readU32BE(stream, offset) || !readU32BE(stream, blockSize))
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    if (chunkSize < 8 + offset)
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    const auto position = stream.tell();
    if (!position.has_value())
    {
        result.error = AiffParseError::BadChunk;
        return false;
    }

    result.header.dataStart = *position + offset;
    result.header.dataSize  = chunkSize - 8 - offset;
    return true;
}

////////////////////////////////////////////////////////////
/// \brief Parse the header of an AIFF/AIFC stream
///
/// The stream is left at an unspecified position.
///
////////////////////////////////////////////////////////////
AiffParseResult parseAiff(sf::InputStream& stream)
{
    AiffParseResult result;

    if (!stream.seek(0).has_value())
    {
        result.error = AiffParseError::NotAiff;
        return result;
    }

    // Parse the FORM chunk header
    std::array<char, 4> formId{};
    std::uint32_t       formSize = 0;
    std::array<char, 4> formType{};
    if (!readBytes(stream, formId.data(), formId.size()) || !readU32BE(stream, formSize) ||
        !readBytes(stream, formType.data(), formType.size()) || std::memcmp(formId.data(), "FORM", 4) != 0 ||
        (std::memcmp(formType.data(), "AIFF", 4) != 0 && std::memcmp(formType.data(), "AIFC", 4) != 0))
    {
        result.error = AiffParseError::NotAiff;
        return result;
    }

    const bool aifc = std::memcmp(formType.data(), "AIFC", 4) == 0;

    // The FORM chunk size covers everything after the 8-byte chunk header, starting with the form type
    const std::uint64_t formEnd = 8 + static_cast<std::uint64_t>(formSize);

    // The stream size allows detecting chunks that declare more data than is actually there
    const auto streamSize = stream.getSize();

    // Iterate over the chunks, looking for COMM and SSND
    bool foundComm = false;
    bool foundSsnd = false;

    std::uint64_t position = 12;
    while (position + 8 <= formEnd)
    {
        if (!stream.seek(position).has_value())
        {
            result.error = AiffParseError::BadChunk;
            return result;
        }

        std::array<char, 4> chunkId{};
        std::uint32_t       chunkSize = 0;
        if (!readBytes(stream, chunkId.data(), chunkId.size()) || !readU32BE(stream, chunkSize))
        {
            result.error = AiffParseError::BadChunk;
            return result;
        }

        const std::uint64_t chunkEnd = position + 8 + chunkSize;

        // A chunk that declares more data than the stream contains is either
        // a truncated SSND chunk or a corrupt chunk structure
        if (streamSize.has_value() && chunkEnd > *streamSize)
        {
            result.error = std::memcmp(chunkId.data(), "SSND", 4) == 0 ? AiffParseError::TruncatedSsnd
                                                                       : AiffParseError::BadChunk;
            return result;
        }

        // A chunk must not extend past the end of its enclosing FORM chunk
        if (chunkEnd > formEnd)
        {
            result.error = AiffParseError::BadChunk;
            return result;
        }

        if (std::memcmp(chunkId.data(), "COMM", 4) == 0)
        {
            if (!parseCommChunk(stream, chunkSize, aifc, result))
                return result;

            foundComm = true;
        }
        else if (std::memcmp(chunkId.data(), "SSND", 4) == 0)
        {
            if (!parseSsndChunk(stream, chunkSize, result))
                return result;

            foundSsnd = true;
        }

        // Chunks are padded to an even size
        position = chunkEnd + (chunkSize & 1u);
    }

    if (!foundComm || !foundSsnd)
    {
        result.error = AiffParseError::MissingChunk;
        return result;
    }

    // Clamp the sample count to the data that is actually available
    const std::uint64_t bytesPerFrame = result.header.channelCount * (result.header.bitsPerSample / 8);
    const std::uint64_t framesInData  = result.header.dataSize / bytesPerFrame;
    result.header.frameCount          = std::min(result.header.frameCount, framesInData);
    result.header.dataSize            = result.header.frameCount * bytesPerFrame;

    return result;
}

////////////////////////////////////////////////////////////
std::vector<sf::SoundChannel> makeChannelMap(unsigned int channelCount)
{
    switch (channelCount)
    {
        case 1:
            return {sf::SoundChannel::Mono};
        case 2:
            return {sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight};
        case 3:
            return {sf::SoundChannel::FrontLeft, sf::SoundChannel::FrontRight, sf::SoundChannel::FrontCenter};
        case 4:
            return {sf::SoundChannel::FrontLeft,
                    sf::SoundChannel::FrontRight,
                    sf::SoundChannel::BackLeft,
                    sf::SoundChannel::BackRight};
        case 5:
            return {sf::SoundChannel::FrontLeft,
                    sf::SoundChannel::FrontRight,
                    sf::SoundChannel::FrontCenter,
                    sf::SoundChannel::BackLeft,
                    sf::SoundChannel::BackRight};
        case 6:
            return {sf::SoundChannel::FrontLeft,
                    sf::SoundChannel::FrontRight,
                    sf::SoundChannel::FrontCenter,
                    sf::SoundChannel::LowFrequencyEffects,
                    sf::SoundChannel::BackLeft,
                    sf::SoundChannel::BackRight};
        case 7:
            return {sf::SoundChannel::FrontLeft,
                    sf::SoundChannel::FrontRight,
                    sf::SoundChannel::FrontCenter,
                    sf::SoundChannel::LowFrequencyEffects,
                    sf::SoundChannel::BackCenter,
                    sf::SoundChannel::SideLeft,
                    sf::SoundChannel::SideRight};
        default:
            return {sf::SoundChannel::FrontLeft,
                    sf::SoundChannel::FrontRight,
                    sf::SoundChannel::FrontCenter,
                    sf::SoundChannel::LowFrequencyEffects,
                    sf::SoundChannel::BackLeft,
                    sf::SoundChannel::BackRight,
                    sf::SoundChannel::SideLeft,
                    sf::SoundChannel::SideRight};
    }
}
} // namespace

namespace sf::priv
{
////////////////////////////////////////////////////////////
bool SoundFileReaderAiff::check(InputStream& stream)
{
    // Run the full header validation so that files this reader cannot handle
    // are skipped and the factory keeps probing the remaining readers
    return parseAiff(stream).error == AiffParseError::None;
}


////////////////////////////////////////////////////////////
std::optional<SoundFileReader::Info> SoundFileReaderAiff::open(InputStream& stream)
{
    const AiffParseResult result = parseAiff(stream);

    switch (result.error)
    {
        case AiffParseError::None:
            break;
        case AiffParseError::NotAiff:
            err() << "Failed to open AIFF file (not an AIFF/AIFC file)" << std::endl;
            return std::nullopt;
        case AiffParseError::BadChunk:
            err() << "Failed to open AIFF file (corrupt chunk structure)" << std::endl;
            return std::nullopt;
        case AiffParseError::MissingChunk:
            err() << "Failed to open AIFF file (missing COMM or SSND chunk)" << std::endl;
            return std::nullopt;
        case AiffParseError::TruncatedSsnd:
            err() << "Failed to open AIFF file (truncated SSND chunk)" << std::endl;
            return std::nullopt;
        case AiffParseError::UnsupportedCompression:
            err() << "Failed to open AIFF file (unsupported AIFC compression type)" << std::endl;
            return std::nullopt;
        case AiffParseError::UnsupportedSampleFormat:
            err() << "Failed to open AIFF file (unsupported sample size)" << std::endl;
            return std::nullopt;
    }

    const AiffHeader& header = result.header;

    m_stream         = &stream;
    m_dataStart      = header.dataStart;
    m_dataSize       = header.dataSize;
    m_bytesRead      = 0;
    m_channelCount   = header.channelCount;
    m_bytesPerSample = header.bitsPerSample / 8;
    m_bytesPerFrame  = m_channelCount * m_bytesPerSample;
    m_littleEndian   = header.littleEndian;

    if (!stream.seek(m_dataStart).has_value())
    {
        err() << "Failed to open AIFF file (failed to seek to the sample data)" << std::endl;
        return std::nullopt;
    }

    return Info{header.frameCount * m_channelCount,
                m_channelCount,
                header.sampleRate,
                makeChannelMap(m_channelCount)};
}


////////////////////////////////////////////////////////////
void SoundFileReaderAiff::seek(std::uint64_t sampleOffset)
{
    assert(m_stream && "AIFF reader not initialized. Call SoundFileReaderAiff::open() to initialize it.");

    const std::uint64_t frameOffset = std::min(sampleOffset / m_channelCount, m_dataSize / m_bytesPerFrame);
    const std::uint64_t byteOffset  = frameOffset * m_bytesPerFrame;

    if (!m_stream->seek(m_dataStart + byteOffset).has_value())
    {
        err() << "Failed to seek AIFF sound stream" << std::endl;
        return;
    }

    m_bytesRead = byteOffset;
}


////////////////////////////////////////////////////////////
std::uint64_t SoundFileReaderAiff::read(std::int16_t* samples, std::uint64_t maxCount)
{
    assert(m_stream && "AIFF reader not initialized. Call SoundFileReaderAiff::open() to initialize it.");

    const std::uint64_t bytesToRead = std::min((maxCount / m_channelCount) * m_bytesPerFrame, m_dataSize - m_bytesRead);
    if (bytesToRead == 0)
        return 0;

    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(bytesToRead));
    const auto                count = m_stream->read(buffer.data(), buffer.size());
    if (!count.has_value())
    {
        err() << "Failed to read from AIFF sound stream" << std::endl;
        return 0;
    }

    // Only whole sample frames can be converted
    const std::uint64_t bytesRead = *count - (*count % m_bytesPerFrame);
    m_bytesRead += bytesRead;

    const std::uint64_t sampleCount = bytesRead / m_bytesPerSample;
    for (std::uint64_t i = 0; i < sampleCount; ++i)
    {
        const std::uint8_t* bytes = buffer.data() + i * m_bytesPerSample;

        switch (m_bytesPerSample)
        {
            case 1:
                // 8-bit AIFF samples are signed
                samples[i] = static_cast<std::int16_t>(static_cast<std::int8_t>(bytes[0]) << 8);
                break;
            case 2:
                samples[i] = m_littleEndian ? static_cast<std::int16_t>((bytes[1] << 8) | bytes[0])
                                            : static_cast<std::int16_t>((bytes[0] << 8) | bytes[1]);
                break;
            case 3:
                // Keep the 16 most significant bits
                samples[i] = m_littleEndian ? static_cast<std::int16_t>((bytes[2] << 8) | bytes[1])
                                            : static_cast<std::int16_t>((bytes[0] << 8) | bytes[1]);
                break;
            default:
                // 32-bit samples, keep the 16 most significant bits
                samples[i] = m_littleEndian ? static_cast<std::int16_t>((bytes[3] << 8) | bytes[2])
                                            : static_cast<std::int16_t>((bytes[0] << 8) | bytes[1]);
                break;
        }
    }

    return sampleCount;
}

} // namespace sf::priv
