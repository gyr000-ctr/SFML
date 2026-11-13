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
#include <SFML/Audio/SoundFileReaderOpus.hpp>

#include <SFML/System/Err.hpp>
#include <SFML/System/InputStream.hpp>

#include <algorithm>
#include <cstring>
#include <ostream>

#include <cassert>

#ifdef SFML_HAS_OPUS
#include <opus/opus_multistream.h>
#endif


#ifdef SFML_HAS_OPUS

namespace
{
// The sample rate Opus always decodes to
constexpr std::uint32_t opusSampleRate = 48000;

// Maximum size of a single Opus packet (sanity limit, packets may span multiple pages)
constexpr std::size_t maxPacketSize = 8 * 1024 * 1024;

// Maximum number of bytes scanned when looking for a page capture pattern
constexpr std::uint64_t maxScanSize = 256 * 1024;

////////////////////////////////////////////////////////////
// Little-endian integer deserialization
////////////////////////////////////////////////////////////
std::uint16_t readU16(const std::uint8_t* data)
{
    return static_cast<std::uint16_t>(data[0] | (data[1] << 8));
}

std::uint32_t readU32(const std::uint8_t* data)
{
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

std::uint64_t readU64(const std::uint8_t* data)
{
    return static_cast<std::uint64_t>(readU32(data)) | (static_cast<std::uint64_t>(readU32(data + 4)) << 32);
}


////////////////////////////////////////////////////////////
// Ogg page checksum (CRC-32 with polynomial 0x04C11DB7, no reflection)
////////////////////////////////////////////////////////////
constexpr std::array<std::uint32_t, 256> makeCrcLookup()
{
    std::array<std::uint32_t, 256> lookup{};
    for (std::uint32_t i = 0; i < lookup.size(); ++i)
    {
        std::uint32_t value = i << 24;
        for (int bit = 0; bit < 8; ++bit)
            value = (value << 1) ^ ((value & 0x80000000u) != 0 ? 0x04c11db7u : 0u);
        lookup[i] = value;
    }
    return lookup;
}

std::uint32_t updateCrc(const std::uint8_t* data, std::size_t size, std::uint32_t crc)
{
    static constexpr auto lookup = makeCrcLookup();
    for (std::size_t i = 0; i < size; ++i)
        crc = (crc << 8) ^ lookup[((crc >> 24) ^ data[i]) & 0xff];
    return crc;
}

bool checkPageCrc(const std::uint8_t* header, std::size_t headerSize, const std::uint8_t* body, std::size_t bodySize)
{
    // The checksum is computed with the CRC field itself set to zero
    const std::uint32_t expected = readU32(header + 22);

    const std::array<std::uint8_t, 4> zeros{};
    std::uint32_t crc = 0;
    crc               = updateCrc(header, 22, crc);
    crc               = updateCrc(zeros.data(), zeros.size(), crc);
    crc               = updateCrc(header + 26, headerSize - 26, crc);
    crc               = updateCrc(body, bodySize, crc);
    return crc == expected;
}


////////////////////////////////////////////////////////////
// Stream helpers
////////////////////////////////////////////////////////////
bool readExact(sf::InputStream& stream, void* data, std::uint64_t size)
{
    auto*         bytes = static_cast<std::uint8_t*>(data);
    std::uint64_t total = 0;
    while (total < size)
    {
        const auto count = stream.read(bytes + total, size - total);
        if (!count || *count == 0)
            return false;
        total += *count;
    }
    return true;
}


////////////////////////////////////////////////////////////
// Ogg page header
////////////////////////////////////////////////////////////
struct PageHeader
{
    std::uint64_t               offset{};    // absolute offset of the page in the stream
    std::uint8_t                headerType{}; // header type flags (continued, BOS, EOS)
    std::int64_t                granule{};   // granule position of the page
    std::uint32_t               serial{};    // serial number of the logical stream
    std::uint8_t                segmentCount{}; // number of segments in the segment table
    std::array<std::uint8_t, 255> segments{};   // segment table
    std::uint32_t               bodySize{};  // total size of the page body in bytes
    std::uint64_t               endOffset{}; // absolute offset of the first byte after the page
};

void parsePageHeader(const std::uint8_t* data, PageHeader& page)
{
    page.headerType   = data[5];
    page.granule      = static_cast<std::int64_t>(readU64(data + 6));
    page.serial       = readU32(data + 14);
    page.segmentCount = data[26];
    std::copy_n(data + 27, page.segmentCount, page.segments.begin());

    page.bodySize = 0;
    for (std::uint8_t i = 0; i < page.segmentCount; ++i)
        page.bodySize += page.segments[i];
}


////////////////////////////////////////////////////////////
/// \brief Scan the stream for the next Ogg capture pattern
///
/// Leaves the stream right after the pattern and returns its offset.
///
////////////////////////////////////////////////////////////
bool scanToCapture(sf::InputStream& stream, std::uint64_t& offset)
{
    std::array<std::uint8_t, 4096> buffer{};

    const auto start = stream.tell();
    if (!start)
        return false;

    std::uint64_t base    = *start; // absolute offset of buffer[0]
    std::size_t   matched = 0;
    std::uint64_t scanned = 0;

    while (scanned < maxScanSize)
    {
        const auto count = stream.read(buffer.data(), std::min<std::uint64_t>(buffer.size(), maxScanSize - scanned));
        if (!count || *count == 0)
            return false;

        for (std::size_t i = 0; i < *count; ++i)
        {
            if (buffer[i] == static_cast<std::uint8_t>("OggS"[matched]))
            {
                if (++matched == 4)
                {
                    offset = base + i + 1 - 4;
                    return stream.seek(offset + 4).has_value();
                }
            }
            else
            {
                matched = (buffer[i] == 'O') ? 1 : 0;
            }
        }

        scanned += *count;
        base += *count;
    }

    return false;
}


////////////////////////////////////////////////////////////
/// \brief Read and validate the Ogg page stored at the given offset
////////////////////////////////////////////////////////////
bool readPageAt(sf::InputStream& stream, std::uint64_t offset, PageHeader& page)
{
    if (!stream.seek(offset).has_value())
        return false;

    std::array<std::uint8_t, 27 + 255> header{};
    if (!readExact(stream, header.data(), 27))
        return false;
    if (std::memcmp(header.data(), "OggS", 4) != 0 || header[4] != 0)
        return false;
    if (!readExact(stream, header.data() + 27, header[26]))
        return false;

    page.offset = offset;
    parsePageHeader(header.data(), page);

    std::vector<std::uint8_t> body(page.bodySize);
    if (!readExact(stream, body.data(), body.size()))
        return false;
    if (!checkPageCrc(header.data(), 27 + page.segmentCount, body.data(), body.size()))
        return false;

    page.endOffset = offset + 27 + page.segmentCount + page.bodySize;
    return true;
}


////////////////////////////////////////////////////////////
/// \brief Parse and validate an Ogg page contained in a memory buffer
////////////////////////////////////////////////////////////
bool parsePageFromBuffer(const std::uint8_t* data, std::size_t size, PageHeader& page)
{
    if (size < 27 || std::memcmp(data, "OggS", 4) != 0 || data[4] != 0)
        return false;

    const std::size_t headerSize = 27 + data[26];
    if (size < headerSize)
        return false;

    PageHeader parsed;
    parsePageHeader(data, parsed);
    if (parsed.bodySize > size - headerSize)
        return false;
    if (!checkPageCrc(data, headerSize, data + headerSize, parsed.bodySize))
        return false;

    page = parsed;
    return true;
}


////////////////////////////////////////////////////////////
/// \brief Find the last valid page of a logical stream before the given offset
////////////////////////////////////////////////////////////
bool findLastPageBefore(sf::InputStream& stream, std::uint32_t serial, std::uint64_t offset, PageHeader& result)
{
    const std::uint64_t windowOffset = (offset > maxScanSize) ? offset - maxScanSize : 0;
    const std::uint64_t windowLength = offset - windowOffset;
    if (windowLength < 27)
        return false;
    if (!stream.seek(windowOffset).has_value())
        return false;

    std::vector<std::uint8_t> window(windowLength);
    if (!readExact(stream, window.data(), window.size()))
        return false;

    for (std::size_t i = window.size() - 4;; --i)
    {
        if (std::memcmp(window.data() + i, "OggS", 4) == 0)
        {
            PageHeader page;
            if (parsePageFromBuffer(window.data() + i, window.size() - i, page) && page.serial == serial)
            {
                page.offset    = windowOffset + i;
                page.endOffset = page.offset + 27 + page.segmentCount + page.bodySize;
                result         = page;
                return true;
            }
        }
        if (i == 0)
            break;
    }

    return false;
}


////////////////////////////////////////////////////////////
/// \brief Find the first valid page of a logical stream at or after the given offset
////////////////////////////////////////////////////////////
bool findPageAfter(sf::InputStream& stream, std::uint32_t serial, std::uint64_t offset, std::uint64_t limit, PageHeader& result)
{
    if (!stream.seek(offset).has_value())
        return false;

    for (unsigned int attempt = 0; attempt < 64; ++attempt)
    {
        std::uint64_t pageOffset = 0;
        if (!scanToCapture(stream, pageOffset) || pageOffset >= limit)
            return false;

        PageHeader page;
        if (!readPageAt(stream, pageOffset, page))
        {
            // Not a valid page: resume scanning right after the capture pattern
            if (!stream.seek(pageOffset + 4).has_value())
                return false;
            continue;
        }

        if (page.serial != serial)
        {
            // Skip pages of other logical streams
            if (!stream.seek(page.endOffset).has_value())
                return false;
            continue;
        }

        result = page;
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////
/// \brief Find the last page of the Opus stream whose granule position
///        is not beyond the given target sample position
///
/// Bisects the stream using page granule positions, then scans the
/// remaining range linearly. The number of page reads is bounded by
/// O(log(fileSize)) plus the size of a single 64 KiB window.
///
////////////////////////////////////////////////////////////
bool findSeekPage(sf::InputStream& stream,
                  std::uint32_t    serial,
                  std::uint64_t    audioStart,
                  std::uint64_t    fileSize,
                  std::uint64_t    target,
                  PageHeader&      best)
{
    std::uint64_t lo    = audioStart;
    std::uint64_t hi    = fileSize;
    bool          found = false;
    PageHeader    page;

    while (hi - lo > 65536)
    {
        const std::uint64_t mid = lo + (hi - lo) / 2;
        if (findPageAfter(stream, serial, mid, hi, page))
        {
            if (page.granule >= 0 && static_cast<std::uint64_t>(page.granule) <= target)
            {
                best  = page;
                found = true;
                lo    = page.endOffset;
            }
            else
            {
                hi = page.offset;
            }
        }
        else
        {
            hi = mid;
        }
    }

    // Scan the remaining range linearly
    std::uint64_t offset = found ? best.offset : audioStart;
    for (unsigned int i = 0; i < 4096 && offset < hi; ++i)
    {
        if (!readPageAt(stream, offset, page))
            break;
        if (page.serial != serial)
        {
            offset = page.endOffset;
            continue;
        }
        if (page.granule >= 0 && static_cast<std::uint64_t>(page.granule) <= target)
        {
            best   = page;
            found  = true;
            offset = page.endOffset;
        }
        else
        {
            break;
        }
    }

    return found;
}


////////////////////////////////////////////////////////////
/// \brief Validate the structure of an OpusTags header packet
////////////////////////////////////////////////////////////
bool validateOpusTags(const std::vector<std::uint8_t>& packet)
{
    // 8 bytes magic, 4 bytes vendor string length, 4 bytes comment count
    if (packet.size() < 16 || std::memcmp(packet.data(), "OpusTags", 8) != 0)
        return false;

    std::size_t pos = 8;

    const auto readLength = [&packet, &pos](std::uint32_t& length)
    {
        if (packet.size() - pos < 4)
            return false;
        length = readU32(packet.data() + pos);
        pos += 4;
        return true;
    };

    const auto skipString = [&packet, &pos](std::uint32_t length)
    {
        if (packet.size() - pos < length)
            return false;
        pos += length;
        return true;
    };

    // Vendor string
    std::uint32_t length = 0;
    if (!readLength(length) || !skipString(length))
        return false;

    // User comment list
    std::uint32_t count = 0;
    if (!readLength(count))
        return false;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (!readLength(length) || !skipString(length))
            return false;
    }

    return true;
}

} // namespace

namespace sf::priv
{
////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::check(InputStream& stream)
{
    // The Opus ID header must be the first packet of the stream, on a beginning-of-stream page
    std::array<std::uint8_t, 27> header{};
    if (!readExact(stream, header.data(), header.size()))
        return false;
    if (std::memcmp(header.data(), "OggS", 4) != 0 || header[4] != 0 || (header[5] & 2) == 0)
        return false;

    const std::uint8_t segmentCount = header[26];
    if (segmentCount == 0)
        return false;

    std::array<std::uint8_t, 255> segments{};
    if (!readExact(stream, segments.data(), segmentCount))
        return false;

    // The ID header is at least 19 bytes long and starts with the "OpusHead" magic
    if (segments[0] < 19)
        return false;

    std::array<std::uint8_t, 19> idHeader{};
    if (!readExact(stream, idHeader.data(), idHeader.size()))
        return false;

    return std::memcmp(idHeader.data(), "OpusHead", 8) == 0;
}


////////////////////////////////////////////////////////////
SoundFileReaderOpus::~SoundFileReaderOpus()
{
    close();
}


////////////////////////////////////////////////////////////
void SoundFileReaderOpus::DecoderDeleter::operator()(OpusMSDecoder* decoder) const
{
    if (decoder)
        opus_multistream_decoder_destroy(decoder);
}


////////////////////////////////////////////////////////////
std::optional<SoundFileReader::Info> SoundFileReaderOpus::open(InputStream& stream)
{
    close();

    m_stream = &stream;

    const auto fail = [this](const char* message)
    {
        err() << "Failed to open Opus file for reading: " << message << std::endl;
        close();
        return std::nullopt;
    };

    if (!stream.seek(0).has_value())
        return fail("stream is not seekable");

    const auto fileSize = stream.getSize();
    if (!fileSize)
        return fail("stream size is not available");
    m_fileSize = *fileSize;

    // Read the ID header packet, which must be the first packet of the stream
    if (!readPacket(m_packet))
        return fail("could not read the OpusHead header");

    if (m_packet.size() < 19 || std::memcmp(m_packet.data(), "OpusHead", 8) != 0)
        return fail("invalid OpusHead header");

    if (m_packet[8] > 15)
        return fail("unsupported OpusHead version");

    const unsigned int channelCount = m_packet[9];
    if (channelCount == 0 || channelCount > maxChannels)
        return fail("unsupported channel count");

    const std::int64_t preSkip = readU16(&m_packet[10]);
    // The input sample rate (bytes 12-15) is informational only: Opus always decodes at 48 kHz
    const auto gain = static_cast<std::int16_t>(readU16(&m_packet[16]));

    const std::uint8_t mappingFamily = m_packet[18];

    unsigned int                      streamCount = 0;
    unsigned int                      coupledCount = 0;
    std::array<unsigned char, maxChannels> mapping{};

    switch (mappingFamily)
    {
        case 0:
            // Mono or stereo without an explicit channel mapping
            if (channelCount > 2)
                return fail("mapping family 0 with more than 2 channels");
            streamCount  = 1;
            coupledCount = (channelCount == 2) ? 1 : 0;
            mapping[0]   = 0;
            mapping[1]   = 1;
            break;
        case 1:
            // Vorbis channel order with an explicit channel mapping table
            if (m_packet.size() < 21 + channelCount)
                return fail("OpusHead header is missing the channel mapping table");
            streamCount  = m_packet[19];
            coupledCount = m_packet[20];
            if (streamCount == 0 || coupledCount > streamCount)
                return fail("invalid stream count in the channel mapping table");
            for (unsigned int i = 0; i < channelCount; ++i)
                mapping[i] = m_packet[21 + i];
            break;
        case 255:
            // Channels with no defined meaning
            if (channelCount > 2)
                return fail("mapping family 255 with more than 2 channels");
            streamCount  = channelCount;
            coupledCount = 0;
            mapping[0]   = 0;
            mapping[1]   = 1;
            break;
        default:
            return fail("unsupported channel mapping family");
    }

    // Read and validate the comment header packet
    if (!readPacket(m_packet))
        return fail("missing OpusTags header");
    if (!validateOpusTags(m_packet))
        return fail("invalid OpusTags header");

    // The OpusTags packet must complete on its own page (RFC 7845), so audio data starts on a fresh page
    if (m_segmentIndex != m_segmentCount)
        return fail("OpusTags header does not end on a page boundary");

    const auto audioStart = stream.tell();
    if (!audioStart)
        return fail("could not determine the start of the audio data");
    m_audioStartOffset = *audioStart;

    // The granule position of the last page of the stream is the total number of
    // decoded samples per channel. Subtracting the pre-skip yields the playable length.
    PageHeader lastPage;
    if (!findLastPageBefore(stream, m_serial, m_fileSize, lastPage))
        return fail("could not determine the stream duration");
    m_totalSamples = std::max<std::int64_t>(lastPage.granule - preSkip, 0);

    // Create the Opus decoder
    int error = OPUS_OK;
    m_decoder.reset(opus_multistream_decoder_create(opusSampleRate,
                                                    static_cast<int>(channelCount),
                                                    static_cast<int>(streamCount),
                                                    static_cast<int>(coupledCount),
                                                    mapping.data(),
                                                    &error));
    if (!m_decoder || error != OPUS_OK)
        return fail("could not create the Opus decoder");

    // Apply the output gain of the OpusHead header (Q7.8 dB, the unit expected by OPUS_SET_GAIN)
    if (gain != 0)
        opus_multistream_decoder_ctl(m_decoder.get(), OPUS_SET_GAIN(gain));

    // Set up the initial decode state. Sample positions are tracked in decoded
    // sample space: the first decoded sample has index 0 and the first playable
    // sample has index preSkip.
    m_channelCount = channelCount;
    m_preSkip      = preSkip;
    m_outputTotal  = 0;
    m_emitFrom     = static_cast<std::uint64_t>(preSkip);
    m_eof          = false;
    m_needPrime    = false;
    m_bufferPos    = 0;
    m_bufferSize   = 0;

    if (!resetDemuxer(m_audioStartOffset))
        return fail("could not seek to the first audio page");

    Info info;
    info.channelCount = channelCount;
    info.sampleRate   = opusSampleRate;
    info.sampleCount  = static_cast<std::uint64_t>(m_totalSamples) * channelCount;

    // For mapping family 1 the channels are in Vorbis order,
    // see: https://xiph.org/vorbis/doc/Vorbis_I_spec.html#x1-810004.3.9
    switch (mappingFamily == 1 ? channelCount : std::min(channelCount, 2u))
    {
        case 1:
            info.channelMap = {SoundChannel::Mono};
            break;
        case 2:
            info.channelMap = {SoundChannel::FrontLeft, SoundChannel::FrontRight};
            break;
        case 3:
            info.channelMap = {SoundChannel::FrontLeft, SoundChannel::FrontCenter, SoundChannel::FrontRight};
            break;
        case 4:
            info.channelMap = {SoundChannel::FrontLeft, SoundChannel::FrontRight, SoundChannel::BackLeft, SoundChannel::BackRight};
            break;
        case 5:
            info.channelMap = {SoundChannel::FrontLeft,
                               SoundChannel::FrontCenter,
                               SoundChannel::FrontRight,
                               SoundChannel::BackLeft,
                               SoundChannel::BackRight};
            break;
        case 6:
            info.channelMap = {SoundChannel::FrontLeft,
                               SoundChannel::FrontCenter,
                               SoundChannel::FrontRight,
                               SoundChannel::BackLeft,
                               SoundChannel::BackRight,
                               SoundChannel::LowFrequencyEffects};
            break;
        case 7:
            info.channelMap = {SoundChannel::FrontLeft,
                               SoundChannel::FrontCenter,
                               SoundChannel::FrontRight,
                               SoundChannel::SideLeft,
                               SoundChannel::SideRight,
                               SoundChannel::BackCenter,
                               SoundChannel::LowFrequencyEffects};
            break;
        case 8:
            info.channelMap = {SoundChannel::FrontLeft,
                               SoundChannel::FrontCenter,
                               SoundChannel::FrontRight,
                               SoundChannel::SideLeft,
                               SoundChannel::SideRight,
                               SoundChannel::BackLeft,
                               SoundChannel::BackRight,
                               SoundChannel::LowFrequencyEffects};
            break;
        default:
            return fail("unsupported channel count");
    }

    return info;
}


////////////////////////////////////////////////////////////
void SoundFileReaderOpus::seek(std::uint64_t sampleOffset)
{
    assert(m_decoder && "Opus decoder is missing. Call SoundFileReaderOpus::open() to initialize it.");

    const std::uint64_t target = sampleOffset / m_channelCount;

    m_bufferPos  = 0;
    m_bufferSize = 0;
    m_eof        = false;
    m_needPrime  = false;

    if (target >= static_cast<std::uint64_t>(m_totalSamples))
    {
        // Jump to the end of the file
        m_eof         = true;
        m_outputTotal = m_totalSamples + m_preSkip;
        return;
    }

    // Work in decoded sample space: granule positions count decoded
    // samples, so the pre-skip is part of the seek target there
    const std::uint64_t decodedTarget = static_cast<std::uint64_t>(m_preSkip) + target;
    m_emitFrom                      = decodedTarget;

    // Find the last page whose granule position is not beyond the target
    PageHeader page;
    bool       usePage = false;
    if (decodedTarget > 0)
        usePage = findSeekPage(*m_stream, m_serial, m_audioStartOffset, m_fileSize, decodedTarget, page);

    if (usePage && page.offset != m_audioStartOffset)
    {
        // If the page starts with a continued packet, back up to the page on which that packet
        // starts, so that decoding always begins at a packet boundary
        for (unsigned int i = 0; i < 8 && (page.headerType & 1) != 0; ++i)
        {
            PageHeader previous;
            if (!findLastPageBefore(*m_stream, m_serial, page.offset, previous))
            {
                usePage = false;
                break;
            }
            page = previous;
        }

        if (usePage)
        {
            // Reset the decoder to its initial state and decode from the located page.
            // The absolute sample position is established by primePosition() on the next read.
            opus_multistream_decoder_ctl(m_decoder.get(), OPUS_RESET_STATE);
            if (resetDemuxer(page.offset))
            {
                m_primePageOffset = page.offset;
                m_needPrime       = true;
                return;
            }
        }
    }

    // Decode from the beginning of the stream
    opus_multistream_decoder_ctl(m_decoder.get(), OPUS_RESET_STATE);
    if (resetDemuxer(m_audioStartOffset))
    {
        m_outputTotal = 0;
    }
    else
    {
        m_eof = true;
    }
}


////////////////////////////////////////////////////////////
std::uint64_t SoundFileReaderOpus::read(std::int16_t* samples, std::uint64_t maxCount)
{
    assert(m_decoder && "Opus decoder is missing. Call SoundFileReaderOpus::open() to initialize it.");

    std::uint64_t count = 0;
    while (count < maxCount)
    {
        if (m_bufferPos == m_bufferSize)
        {
            if (m_eof || !fillBuffer())
            {
                m_eof = true;
                break;
            }
        }

        const std::uint64_t chunk = std::min(maxCount - count, static_cast<std::uint64_t>(m_bufferSize - m_bufferPos));
        std::memcpy(samples + count, m_decodeBuffer.data() + m_bufferPos, chunk * sizeof(std::int16_t));
        m_bufferPos += static_cast<std::size_t>(chunk);
        count += chunk;
    }

    return count;
}


////////////////////////////////////////////////////////////
void SoundFileReaderOpus::close()
{
    m_decoder.reset();
    m_stream           = nullptr;
    m_fileSize         = 0;
    m_channelCount     = 0;
    m_serial           = 0;
    m_serialKnown      = false;
    m_audioStartOffset = 0;
    m_preSkip          = 0;
    m_totalSamples     = 0;
    m_outputTotal      = 0;
    m_emitFrom         = 0;
    m_eof              = true;
    m_needPrime        = false;
    m_primePageOffset  = 0;

    m_pageHeaderType       = 0;
    m_pageGranule          = 0;
    m_pageSerial           = 0;
    m_pageOffset           = 0;
    m_segmentCount         = 0;
    m_segmentIndex         = 0;
    m_pageBodyPos          = 0;
    m_lastPacketGranule    = 0;
    m_lastPacketPageOffset = 0;
    m_pageBody.clear();
    m_packet.clear();

    m_bufferPos  = 0;
    m_bufferSize = 0;
}


////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::loadPage()
{
    for (unsigned int attempt = 0; attempt < 64; ++attempt)
    {
        if (!scanToCapture(*m_stream, m_pageOffset))
            return false;

        std::array<std::uint8_t, 27 + 255> header{};
        std::memcpy(header.data(), "OggS", 4);
        if (!readExact(*m_stream, header.data() + 4, 23))
            return false;
        if (!readExact(*m_stream, header.data() + 27, header[26]))
            return false;
        if (header[4] != 0)
            continue; // unsupported Ogg version: keep scanning

        PageHeader page;
        page.offset = m_pageOffset;
        parsePageHeader(header.data(), page);

        m_pageBody.resize(page.bodySize);
        if (!readExact(*m_stream, m_pageBody.data(), m_pageBody.size()))
            return false;
        if (!checkPageCrc(header.data(), 27 + page.segmentCount, m_pageBody.data(), m_pageBody.size()))
            continue; // corrupt page: keep scanning

        m_pageHeaderType = page.headerType;
        m_pageGranule    = page.granule;
        m_pageSerial     = page.serial;
        m_segments       = page.segments;
        m_segmentCount   = page.segmentCount;
        m_segmentIndex   = 0;
        m_pageBodyPos    = 0;
        return true;
    }

    return false;
}


////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::readNextPage()
{
    for (;;)
    {
        if (!loadPage())
            return false;

        if (!m_serialKnown)
        {
            // The first page of the stream must be a beginning-of-stream page
            if ((m_pageHeaderType & 2) == 0)
                return false;
            m_serial      = m_pageSerial;
            m_serialKnown = true;
            return true;
        }

        if (m_pageSerial == m_serial)
            return true;

        // Skip pages of other logical streams
    }
}


////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::readPacket(std::vector<std::uint8_t>& packet)
{
    packet.clear();
    bool inProgress = false;

    for (;;)
    {
        if (m_segmentIndex == m_segmentCount)
        {
            // The logical stream ends after its end-of-stream page
            if ((m_pageHeaderType & 4) != 0)
                return false;

            if (!readNextPage())
                return false;

            const bool continued = (m_pageHeaderType & 1) != 0;
            if (inProgress && !continued)
            {
                // The packet did not finish on the previous page: the stream is corrupt, resynchronize
                packet.clear();
                inProgress = false;
            }
            else if (!inProgress && continued)
            {
                // Skip the remainder of a packet that started on a page that was not read
                bool done = false;
                while (!done)
                {
                    while (m_segmentIndex < m_segmentCount)
                    {
                        const std::uint8_t lacing = m_segments[m_segmentIndex++];
                        m_pageBodyPos += lacing;
                        if (lacing < 255)
                        {
                            done = true;
                            break;
                        }
                    }
                    if (!done)
                    {
                        if (!readNextPage())
                            return false;
                        if ((m_pageHeaderType & 1) == 0)
                            break; // lost synchronization: stop skipping
                    }
                }
                continue;
            }
        }

        const std::uint8_t lacing = m_segments[m_segmentIndex++];
        if (packet.size() + lacing > maxPacketSize)
            return false;
        packet.insert(packet.end(), m_pageBody.begin() + m_pageBodyPos, m_pageBody.begin() + m_pageBodyPos + lacing);
        m_pageBodyPos += lacing;

        if (lacing < 255)
        {
            m_lastPacketGranule    = m_pageGranule;
            m_lastPacketPageOffset = m_pageOffset;
            return true;
        }

        inProgress = true;
    }
}


////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::resetDemuxer(std::uint64_t offset)
{
    if (!m_stream->seek(offset).has_value())
        return false;

    m_pageHeaderType = 0;
    m_segmentCount   = 0;
    m_segmentIndex   = 0;
    m_pageBodyPos    = 0;
    m_pageBody.clear();
    m_pageOffset = offset;
    return true;
}


////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::primePosition()
{
    // Count the samples of the packets completing on the first granule-bearing
    // page from the current position. The granule position of that page is the
    // absolute index of the sample right after those packets.
    std::uint64_t frames = 0;

    for (;;)
    {
        if (!readPacket(m_packet))
            return false;

        const int packetFrames = opus_packet_get_nb_samples(m_packet.data(),
                                                            static_cast<opus_int32>(m_packet.size()),
                                                            static_cast<opus_int32>(opusSampleRate));
        if (packetFrames >= 0)
            frames += static_cast<std::uint64_t>(packetFrames);

        if (m_lastPacketGranule >= 0)
        {
            m_outputTotal = m_lastPacketGranule - static_cast<std::int64_t>(frames);
            return resetDemuxer(m_primePageOffset);
        }
    }
}


////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::fillBuffer()
{
    if (m_needPrime)
    {
        m_needPrime = false;
        if (!primePosition())
        {
            // Fall back to decoding from the beginning of the stream
            if (!resetDemuxer(m_audioStartOffset))
                return false;
            m_outputTotal = 0;
        }
    }

    for (;;)
    {
        if (!readPacket(m_packet))
            return false;

        const int frames = opus_multistream_decode(m_decoder.get(),
                                                   m_packet.data(),
                                                   static_cast<opus_int32>(m_packet.size()),
                                                   m_decodeBuffer.data(),
                                                   static_cast<int>(maxFrameSize),
                                                   0);
        if (frames <= 0)
            continue; // skip undecodable packets

        std::uint64_t count = static_cast<std::uint64_t>(frames);

        // Trim samples beyond the granule position of the page on which the packet completed
        if (m_lastPacketGranule >= 0 && m_outputTotal + static_cast<std::int64_t>(count) > m_lastPacketGranule)
        {
            const std::int64_t allowed = m_lastPacketGranule - m_outputTotal;
            count                      = static_cast<std::uint64_t>(std::max<std::int64_t>(allowed, 0));
        }

        // Discard samples preceding the emit position (pre-skip and seek target)
        std::uint64_t skip = 0;
        if (m_outputTotal < static_cast<std::int64_t>(m_emitFrom))
        {
            skip = std::min(count, static_cast<std::uint64_t>(static_cast<std::int64_t>(m_emitFrom) - m_outputTotal));
        }

        m_outputTotal += static_cast<std::int64_t>(count);
        count -= skip;

        if (count == 0)
            continue;

        if (skip > 0)
        {
            std::memmove(m_decodeBuffer.data(),
                         m_decodeBuffer.data() + skip * m_channelCount,
                         static_cast<std::size_t>(count * m_channelCount) * sizeof(std::int16_t));
        }

        m_bufferPos  = 0;
        m_bufferSize = static_cast<std::size_t>(count) * m_channelCount;
        return true;
    }
}

} // namespace sf::priv

#else // SFML_HAS_OPUS

namespace sf::priv
{
////////////////////////////////////////////////////////////
bool SoundFileReaderOpus::check(InputStream&)
{
    // Opus support is not compiled in: never claim any file
    return false;
}


////////////////////////////////////////////////////////////
SoundFileReaderOpus::~SoundFileReaderOpus() = default;


////////////////////////////////////////////////////////////
void SoundFileReaderOpus::DecoderDeleter::operator()(OpusMSDecoder*) const
{
}


////////////////////////////////////////////////////////////
std::optional<SoundFileReader::Info> SoundFileReaderOpus::open(InputStream&)
{
    err() << "Failed to open Opus file for reading: SFML was built without Opus support" << std::endl;
    return std::nullopt;
}


////////////////////////////////////////////////////////////
void SoundFileReaderOpus::seek(std::uint64_t)
{
    assert(false && "Opus decoder is missing. Call SoundFileReaderOpus::open() to initialize it.");
}


////////////////////////////////////////////////////////////
std::uint64_t SoundFileReaderOpus::read(std::int16_t*, std::uint64_t)
{
    assert(false && "Opus decoder is missing. Call SoundFileReaderOpus::open() to initialize it.");
    return 0;
}


////////////////////////////////////////////////////////////
void SoundFileReaderOpus::close()
{
}

} // namespace sf::priv

#endif // SFML_HAS_OPUS
