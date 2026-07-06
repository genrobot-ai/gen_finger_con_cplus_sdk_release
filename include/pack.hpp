/**
 * @file pack.hpp
 * @brief DAS command packing and message parsing.
 */

#ifndef FINGER_PACK_HPP
#define FINGER_PACK_HPP

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace das {

enum class Opcode : uint8_t {
    ReadSingle = 0x01,
    ReadBatch = 0x02,
    WriteDrive = 0x03,
    Echo = 0x04,
    CalibEncoder = 0x05,
    DisableDrive = 0x06
};

enum class RecordType : uint8_t {
    Tactile = 0x01,
    Encoder = 0x02,
    Drive = 0x03,
    Echo = 0x04
};

struct PackContent {
    static constexpr char MAGIC[] = "das\r\n";
    static constexpr size_t MAGIC_LENGTH = 5;
};

struct Record {
    RecordType record_type;
    size_t record_content_length;
    std::vector<uint8_t> record_data;

    Record(RecordType type, const std::vector<uint8_t>& data)
        : record_type(type), record_content_length(data.size()), record_data(data) {}

    std::vector<uint8_t> pack() const;
};

class CmdPack {
public:
    std::vector<uint8_t> data;
    std::optional<Opcode> opcode;
    std::optional<RecordType> record_type;
    std::vector<uint8_t> record_data;

    CmdPack() = default;
    CmdPack(const std::vector<uint8_t>& data,
            std::optional<Opcode> opcode = std::nullopt,
            std::optional<RecordType> record_type = std::nullopt,
            const std::vector<uint8_t>& record_data = {});

    static CmdPack pack(Opcode opcode, RecordType record_type, const std::vector<uint8_t>& record = {});
    static CmdPack packCalib(const std::vector<uint8_t>& record);
    static std::optional<CmdPack> unpack(const std::vector<uint8_t>& data);
};

class MessagePack {
public:
    std::vector<uint8_t> data;
    std::optional<Opcode> opcode;
    std::vector<Record> records;

    struct CalibInfo {
        uint32_t width = 0;
        uint32_t height = 0;
        std::string model;
        std::vector<double> distortion;
        std::vector<double> intrinsics;
        std::vector<double> extrinsics;
    };

    MessagePack() = default;
    MessagePack(const std::vector<uint8_t>& data,
                std::optional<Opcode> opcode = std::nullopt,
                const std::vector<Record>& records = {});

    static std::vector<uint8_t> pack(Opcode opcode, const std::vector<Record>& records = {});
    static std::optional<MessagePack> unpack(const std::vector<uint8_t>& data);
    static bool unpackCameraCalib(const std::vector<uint8_t>& data,
                                  const std::string& yaml_filename = "",
                                  const std::string& output_dir = "",
                                  const std::string& calib_cmd_name = "");

private:
    static bool checkHead(const std::vector<uint8_t>& data);
    static bool checkTail(const std::vector<uint8_t>& data);
    static bool checkMagic(const uint8_t* data, size_t length);
    static std::pair<uint64_t, size_t> readVarint(const std::vector<uint8_t>& data, size_t pos);
    static CalibInfo parseProtobufCalib(const std::vector<uint8_t>& data);
    static std::string generateYaml(const CalibInfo& info);
};

inline std::vector<uint8_t> floatToBigEndianBytes(float value) {
    std::vector<uint8_t> bytes(4);
    uint32_t val = 0;
    std::memcpy(&val, &value, sizeof(value));
    bytes[0] = static_cast<uint8_t>((val >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((val >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((val >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(val & 0xFF);
    return bytes;
}

inline float bigEndianBytesToFloat(const uint8_t* data) {
    uint32_t val = (static_cast<uint32_t>(data[0]) << 24) |
                   (static_cast<uint32_t>(data[1]) << 16) |
                   (static_cast<uint32_t>(data[2]) << 8) |
                   static_cast<uint32_t>(data[3]);
    float value = 0.0f;
    std::memcpy(&value, &val, sizeof(value));
    return value;
}

} // namespace das

#endif // FINGER_PACK_HPP
