#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#include "wind_usb_protocol.h"
}

namespace {
struct Frames {
    std::vector<uint32_t> ids;
    std::vector<std::string> payloads;
};

void collect(const wind_usb_frame_t *frame, void *context)
{
    auto *frames = static_cast<Frames *>(context);
    frames->ids.push_back(frame->request_id);
    frames->payloads.emplace_back(reinterpret_cast<const char *>(frame->payload), frame->payload_length);
}
}

TEST(WindUsbProtocolTest, ReassemblesAFrameAcrossArbitraryChunks)
{
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char payload[] = R"({"command":"hello"})";
    size_t size = wind_usb_encode_frame(42, WIND_USB_MESSAGE_REQUEST,
                                        reinterpret_cast<const uint8_t *>(payload),
                                        strlen(payload), encoded.data(), encoded.size());
    ASSERT_GT(size, 0u);

    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    for (size_t offset = 0; offset < size; offset += 3) {
        const size_t chunk = std::min<size_t>(3, size - offset);
        EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data() + offset, chunk, collect, &frames).error,
                  ESP_OK);
    }
    ASSERT_EQ(frames.ids.size(), 1u);
    EXPECT_EQ(frames.ids[0], 42u);
    EXPECT_EQ(frames.payloads[0], payload);
}

TEST(WindUsbProtocolTest, DropsCorruptOversizedAndDuplicateFrames)
{
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;

    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char payload[] = R"({"command":"get_state"})";
    size_t size = wind_usb_encode_frame(7, WIND_USB_MESSAGE_REQUEST,
                                        reinterpret_cast<const uint8_t *>(payload), strlen(payload),
                                        encoded.data(), encoded.size());
    ASSERT_GT(size, 0u);
    encoded[size - 1] ^= 0xff;
    EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error,
              ESP_ERR_INVALID_CRC);
    EXPECT_TRUE(frames.ids.empty());

    wind_usb_parser_init(&parser);
    size = wind_usb_encode_frame(7, WIND_USB_MESSAGE_REQUEST,
                                 reinterpret_cast<const uint8_t *>(payload), strlen(payload),
                                 encoded.data(), encoded.size());
    ASSERT_GT(size, 0u);
    EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    EXPECT_EQ(frames.ids.size(), 1u);

    std::array<uint8_t, WIND_USB_HEADER_SIZE> oversized{};
    memcpy(oversized.data(), WIND_USB_MAGIC, WIND_USB_MAGIC_SIZE);
    wind_usb_write_u16(oversized.data() + 8, WIND_USB_PROTOCOL_VERSION);
    wind_usb_write_u32(oversized.data() + 16, WIND_USB_MAX_PAYLOAD + 1);
    wind_usb_parser_init(&parser);
    EXPECT_EQ(wind_usb_parser_feed(&parser, oversized.data(), oversized.size(), collect, &frames).error,
              ESP_ERR_INVALID_SIZE);
}

TEST(WindUsbProtocolTest, RecoversAfterNoiseAndNeverEchoesInput)
{
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    const std::array<uint8_t, 17> noise = {0xff, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    EXPECT_EQ(wind_usb_parser_feed(&parser, noise.data(), noise.size(), collect, &frames).error, ESP_OK);

    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char payload[] = R"({"command":"hello"})";
    size_t size = wind_usb_encode_frame(9, WIND_USB_MESSAGE_REQUEST,
                                        reinterpret_cast<const uint8_t *>(payload), strlen(payload),
                                        encoded.data(), encoded.size());
    EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    ASSERT_EQ(frames.payloads.size(), 1u);
    EXPECT_EQ(frames.payloads.front(), payload);
}

TEST(WindUsbProtocolTest, HelloStartsANewBrowserSessionAtRequestOne)
{
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char state[] = R"({"command":"get_state"})";
    size_t size = wind_usb_encode_frame(9, WIND_USB_MESSAGE_REQUEST,
                                        reinterpret_cast<const uint8_t *>(state), strlen(state),
                                        encoded.data(), encoded.size());
    ASSERT_GT(size, 0u);
    EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);

    const char hello[] = R"({ "client": "browser", "command" : "hello" })";
    size = wind_usb_encode_frame(1, WIND_USB_MESSAGE_REQUEST,
                                 reinterpret_cast<const uint8_t *>(hello), strlen(hello),
                                 encoded.data(), encoded.size());
    ASSERT_GT(size, 0u);
    EXPECT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    ASSERT_EQ(frames.ids.size(), 2u);
    EXPECT_EQ(frames.ids.back(), 1u);
}

TEST(WindUsbProtocolTest, InterruptedLargeRequestMustNotBlockANewSession) {
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const std::string interrupted(8192, 'x');
    ASSERT_GT(wind_usb_encode_frame(8, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(interrupted.data()), interrupted.size(),
        encoded.data(), encoded.size()), 0u);
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), 100, collect, &frames).error, ESP_OK);
    EXPECT_FALSE(wind_usb_parser_expire_partial(&parser, WIND_USB_PARTIAL_TIMEOUT_MS - 1));
    EXPECT_EQ(parser.length, 100u);
    EXPECT_TRUE(wind_usb_parser_expire_partial(&parser, WIND_USB_PARTIAL_TIMEOUT_MS));
    const char hello[] = R"({"command":"hello"})";
    const auto size = wind_usb_encode_frame(1, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(hello), strlen(hello), encoded.data(), encoded.size());
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    ASSERT_EQ(frames.ids.size(), 1u);
    EXPECT_EQ(frames.ids.back(), 1u);
}

TEST(WindUsbProtocolTest, ExpiringPartialBytesPreservesDuplicateProtection) {
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char payload[] = R"({"command":"get_state"})";
    const auto size = wind_usb_encode_frame(12, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(payload), strlen(payload), encoded.data(), encoded.size());
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), 13, collect, &frames).error, ESP_OK);
    ASSERT_TRUE(wind_usb_parser_expire_partial(&parser, WIND_USB_PARTIAL_TIMEOUT_MS));
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    EXPECT_EQ(frames.ids.size(), 1u);
    EXPECT_FALSE(wind_usb_parser_expire_partial(&parser, WIND_USB_PARTIAL_TIMEOUT_MS));
}

TEST(WindUsbProtocolTest, OversizedHeaderDoesNotDiscardTheFollowingValidFrame) {
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_HEADER_SIZE> oversized{};
    memcpy(oversized.data(), WIND_USB_MAGIC, WIND_USB_MAGIC_SIZE);
    wind_usb_write_u16(oversized.data() + 8, WIND_USB_PROTOCOL_VERSION);
    wind_usb_write_u32(oversized.data() + 16, WIND_USB_MAX_PAYLOAD + 1);
    std::vector<uint8_t> input(oversized.begin(), oversized.end());
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char hello[] = R"({"command":"hello"})";
    const auto size = wind_usb_encode_frame(1, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(hello), strlen(hello), encoded.data(), encoded.size());
    input.insert(input.end(), encoded.begin(), encoded.begin() + size);
    const auto result = wind_usb_parser_feed(&parser, input.data(), input.size(), collect, &frames);
    EXPECT_EQ(result.error, ESP_ERR_INVALID_SIZE);
    EXPECT_EQ(result.delivered_frames, 1u);
    ASSERT_EQ(frames.ids.size(), 1u);
    EXPECT_EQ(frames.payloads[0], hello);
}

TEST(WindUsbProtocolTest, MalformedLengthMustNotPermitReplayingAnAppliedCommand) {
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char payload[] = R"({"command":"apply_configuration"})";
    const auto size = wind_usb_encode_frame(12, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(payload), strlen(payload), encoded.data(), encoded.size());
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    std::array<uint8_t, WIND_USB_HEADER_SIZE> oversized{};
    memcpy(oversized.data(), WIND_USB_MAGIC, WIND_USB_MAGIC_SIZE);
    wind_usb_write_u16(oversized.data() + 8, WIND_USB_PROTOCOL_VERSION);
    wind_usb_write_u32(oversized.data() + 16, WIND_USB_MAX_PAYLOAD + 1);
    ASSERT_EQ(wind_usb_parser_feed(&parser, oversized.data(), oversized.size(), collect, &frames).error,
              ESP_ERR_INVALID_SIZE);
    ASSERT_EQ(wind_usb_parser_feed(&parser, encoded.data(), size, collect, &frames).error, ESP_OK);
    EXPECT_EQ(frames.ids.size(), 1u);
}

TEST(WindUsbProtocolTest, MaximumPayloadSurvivesRandomChunkBoundariesAndFollowingFrame) {
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    std::string payload(WIND_USB_MAX_PAYLOAD, '\0');
    for (size_t i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(i % 256);
    const auto size = wind_usb_encode_frame(12, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(payload.data()), payload.size(), encoded.data(), encoded.size());
    ASSERT_EQ(size, WIND_USB_MAX_FRAME_SIZE);
    std::vector<uint8_t> stream(encoded.begin(), encoded.end());
    const char next[] = R"({"command":"get_state"})";
    const auto next_size = wind_usb_encode_frame(13, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(next), strlen(next), encoded.data(), encoded.size());
    stream.insert(stream.end(), encoded.begin(), encoded.begin() + next_size);
    uint32_t random = 42;
    for (size_t offset = 0; offset < stream.size();) {
        random = random * 1664525u + 1013904223u;
        const size_t chunk = std::min<size_t>(1 + random % 513, stream.size() - offset);
        ASSERT_EQ(wind_usb_parser_feed(&parser, stream.data() + offset, chunk, collect, &frames).error, ESP_OK);
        offset += chunk;
    }
    ASSERT_EQ(frames.ids, (std::vector<uint32_t>{12, 13}));
    ASSERT_EQ(frames.payloads.size(), 2u);
    EXPECT_EQ(frames.payloads[0], payload);
    EXPECT_EQ(frames.payloads[1], next);
}

TEST(WindUsbProtocolTest, TruncatedHeaderPreservesFollowingSessionMagic) {
    wind_usb_parser_t parser;
    wind_usb_parser_init(&parser);
    Frames frames;
    std::array<uint8_t, WIND_USB_MAX_FRAME_SIZE> encoded{};
    const char hello[] = R"({"command":"hello"})";
    const auto size = wind_usb_encode_frame(1, WIND_USB_MESSAGE_REQUEST,
        reinterpret_cast<const uint8_t *>(hello), strlen(hello), encoded.data(), encoded.size());
    std::vector<uint8_t> stream(encoded.begin(), encoded.begin() + 16);
    stream.insert(stream.end(), encoded.begin(), encoded.begin() + size);
    const auto result = wind_usb_parser_feed(&parser, stream.data(), stream.size(), collect, &frames);
    EXPECT_EQ(result.error, ESP_ERR_INVALID_SIZE);
    EXPECT_EQ(result.delivered_frames, 1u);
    ASSERT_EQ(frames.payloads, (std::vector<std::string>{hello}));
}
