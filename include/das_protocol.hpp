/**
 * @file das_protocol.hpp
 * @brief DAS packet framing helpers.
 */

#ifndef FINGER_DAS_PROTOCOL_HPP
#define FINGER_DAS_PROTOCOL_HPP

#include <chrono>
#include <cstdint>
#include <optional>
#include <tuple>
#include <vector>

namespace das {

class DASProtocol {
public:
    static constexpr char MAGIC[] = "das\r\n";
    static constexpr size_t MAGIC_LENGTH = 5;
    static constexpr size_t MAX_PACKET_SIZE = 4096;
    static constexpr size_t MAX_BUFFER_SIZE = 8192;

    struct PacketInfo {
        uint8_t opcode;
        std::vector<uint8_t> data_section;
        size_t data_length;
        std::vector<uint8_t> raw_packet;
        size_t packet_length;
        std::chrono::system_clock::time_point timestamp;
    };

    static std::tuple<std::vector<std::vector<uint8_t>>, std::vector<uint8_t>>
    findPacket(const std::vector<uint8_t>& data);

    static bool validatePacketStructure(const std::vector<uint8_t>& packet);
    static std::optional<PacketInfo> parsePacket(const std::vector<uint8_t>& packet);
    static std::vector<uint8_t> createPacket(uint8_t opcode, const std::vector<uint8_t>& data = {});

private:
    static bool checkMagic(const uint8_t* data, size_t length);
};

} // namespace das

#endif // FINGER_DAS_PROTOCOL_HPP
