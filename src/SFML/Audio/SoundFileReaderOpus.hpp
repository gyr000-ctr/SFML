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

#pragma once

////////////////////////////////////////////////////////////
// Headers
////////////////////////////////////////////////////////////
#include <SFML/Audio/SoundFileReader.hpp>

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include <cstdint>


struct OpusMSDecoder;

namespace sf
{
class InputStream;
}

namespace sf::priv
{
////////////////////////////////////////////////////////////
/// \brief Implementation of sound file reader that handles OGG/Opus files
///
/// This reader is only functional when SFML is built with Opus
/// support (`SFML_HAS_OPUS`). Otherwise every check fails and no
/// instance of this reader is ever created by the factory.
///
////////////////////////////////////////////////////////////
class SoundFileReaderOpus : public SoundFileReader
{
public:
    ////////////////////////////////////////////////////////////
    /// \brief Check if this reader can handle a file given by an input stream
    ///
    /// \param stream Source stream to check
    ///
    /// \return `true` if the file is supported by this reader
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] static bool check(InputStream& stream);

    ////////////////////////////////////////////////////////////
    /// \brief Destructor
    ///
    ////////////////////////////////////////////////////////////
    ~SoundFileReaderOpus() override;

    ////////////////////////////////////////////////////////////
    /// \brief Open a sound file for reading
    ///
    /// \param stream Source stream to read from
    ///
    /// \return Properties of the loaded sound if the file was successfully opened
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] std::optional<Info> open(InputStream& stream) override;

    ////////////////////////////////////////////////////////////
    /// \brief Change the current read position to the given sample offset
    ///
    /// The sample offset takes the channels into account.
    /// If you have a time offset instead, you can easily find
    /// the corresponding sample offset with the following formula:
    /// `timeInSeconds * sampleRate * channelCount`
    /// If the given offset exceeds to total number of samples,
    /// this function must jump to the end of the file.
    ///
    /// \param sampleOffset Index of the sample to jump to, relative to the beginning
    ///
    ////////////////////////////////////////////////////////////
    void seek(std::uint64_t sampleOffset) override;

    ////////////////////////////////////////////////////////////
    /// \brief Read audio samples from the open file
    ///
    /// \param samples  Pointer to the sample array to fill
    /// \param maxCount Maximum number of samples to read
    ///
    /// \return Number of samples actually read (may be less than \a maxCount)
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] std::uint64_t read(std::int16_t* samples, std::uint64_t maxCount) override;

private:
    ////////////////////////////////////////////////////////////
    /// \brief Deleter releasing a libopus multistream decoder
    ///
    ////////////////////////////////////////////////////////////
    struct DecoderDeleter
    {
        void operator()(OpusMSDecoder* decoder) const;
    };

    ////////////////////////////////////////////////////////////
    /// \brief Close the open Opus file and release all resources
    ///
    ////////////////////////////////////////////////////////////
    void close();

    ////////////////////////////////////////////////////////////
    /// \brief Scan for, read and validate the next Ogg page
    ///
    /// The page body is loaded into memory and the page state members
    /// are updated. Pages that fail validation are skipped.
    ///
    /// \return `true` if a valid page was loaded, `false` on end of stream or unrecoverable error
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool loadPage();

    ////////////////////////////////////////////////////////////
    /// \brief Read the next Ogg page of the Opus logical stream
    ///
    /// Pages belonging to other logical streams are skipped.
    ///
    /// \return `true` if a page of the Opus stream was loaded, `false` on end of stream or unrecoverable error
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool readNextPage();

    ////////////////////////////////////////////////////////////
    /// \brief Read the next complete Opus packet, reassembling it across pages if necessary
    ///
    /// \param packet Storage receiving the packet data
    ///
    /// \return `true` if a packet was read, `false` on end of stream or unrecoverable error
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool readPacket(std::vector<std::uint8_t>& packet);

    ////////////////////////////////////////////////////////////
    /// \brief Reposition the stream and the demuxer state at the given page offset
    ///
    /// \param offset Absolute offset of an Ogg page of the Opus logical stream
    ///
    /// \return `true` on success
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool resetDemuxer(std::uint64_t offset);

    ////////////////////////////////////////////////////////////
    /// \brief Determine the absolute sample position after a seek
    ///
    /// Counts the samples of the packets completing on the first
    /// granule-bearing page from the current position, without
    /// decoding audio, then rewinds to the seek page.
    ///
    /// \return `true` on success
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool primePosition();

    ////////////////////////////////////////////////////////////
    /// \brief Decode packets until at least one sample is available or the stream ends
    ///
    /// \return `true` if samples were decoded into the sample buffer
    ///
    ////////////////////////////////////////////////////////////
    [[nodiscard]] bool fillBuffer();

    ////////////////////////////////////////////////////////////
    // Member data
    ////////////////////////////////////////////////////////////
    static constexpr std::uint64_t maxFrameSize = 5760; // maximum Opus frame size: 120 ms at 48 kHz
    static constexpr unsigned int  maxChannels  = 8;    // maximum supported channel count

    std::unique_ptr<OpusMSDecoder, DecoderDeleter> m_decoder;   // libopus multistream decoder
    InputStream*  m_stream{};                                   // source stream (not owned)
    std::uint64_t m_fileSize{};                                 // total size of the stream in bytes
    unsigned int  m_channelCount{};                             // number of channels of the open sound file
    std::uint32_t m_serial{};                                   // serial number of the Opus logical stream
    bool          m_serialKnown{};                              // whether the stream serial number has been determined
    std::uint64_t m_audioStartOffset{};                         // offset of the first audio page in the stream
    std::int64_t  m_preSkip{};                                  // number of leading samples to discard
    std::int64_t  m_totalSamples{};                             // total number of samples per channel (pre-skip adjusted)
    std::int64_t  m_outputTotal{};                              // absolute index of the next decoded frame
    std::uint64_t m_emitFrom{};                                 // absolute index of the first frame to emit
    bool          m_eof{true};                                  // end of stream reached (or reader not open)
    bool          m_needPrime{};                                // absolute position must be established before decoding
    std::uint64_t m_primePageOffset{};                          // offset of the page to rewind to after priming

    // Ogg page demuxer state
    std::uint8_t                m_pageHeaderType{};             // header type flags of the current page
    std::int64_t                m_pageGranule{};                // granule position of the current page
    std::uint32_t               m_pageSerial{};                 // serial number of the current page
    std::uint64_t               m_pageOffset{};                 // absolute offset of the current page in the stream
    std::array<std::uint8_t, 255> m_segments{};                 // segment table of the current page
    std::uint8_t                m_segmentCount{};               // number of segments of the current page
    std::uint8_t                m_segmentIndex{};               // index of the next segment to consume
    std::vector<std::uint8_t>   m_pageBody;                     // body of the current page
    std::uint32_t               m_pageBodyPos{};                // read position within the current page body
    std::int64_t                m_lastPacketGranule{};          // granule position of the page on which the last packet completed
    std::uint64_t               m_lastPacketPageOffset{};       // offset of the page on which the last packet completed
    std::vector<std::uint8_t>   m_packet;                       // reusable packet storage

    // Decode buffer state
    std::array<std::int16_t, maxFrameSize * maxChannels> m_decodeBuffer; // decoded samples of a single packet
    std::size_t m_bufferPos{};                                  // read position within the decode buffer
    std::size_t m_bufferSize{};                                 // number of valid samples in the decode buffer
};

} // namespace sf::priv
