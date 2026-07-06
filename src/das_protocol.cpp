/**
 * @file das_protocol.cpp
 * @brief DAS packet framing helpers implementation.
 */

#include "das_protocol.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace das {

bool DASProtocol::checkMagic(const uint8_t* data, size_t length) {
    return length >= MAGIC_LENGTH && std::memcmp(data, MAGIC, MAGIC_LENGTH) == 0;
}

std::tuple<std::vector<std::vector<uint8_t>>, std::vector<uint8_t>>
DASProtocol::findPacket(const std::vector<uint8_t>& data) {
    std::vector<std::vector<uint8_t>> packets;
    std::vector<uint8_t> buffer = data;

    if (buffer.size() > MAX_BUFFER_SIZE) {
        return {packets, {}};
    }

    size_t search_start = 0;
    size_t processed_count = 0;
    while (buffer.size() >= search_start + MAGIC_LENGTH * 2) {
        if (++processed_count > 1000) {
            break;
        }

        auto header_it = std::search(
            buffer.begin() + search_start, buffer.end(),
            MAGIC, MAGIC + MAGIC_LENGTH
        );
        if (header_it == buffer.end()) {
            break;
        }

        const size_t header_pos = static_cast<size_t>(header_it - buffer.begin());
        const size_t footer_search_start = header_pos + MAGIC_LENGTH;
        auto footer_it = std::search(
            buffer.begin() + footer_search_start, buffer.end(),
            MAGIC, MAGIC + MAGIC_LENGTH
        );
        if (footer_it == buffer.end()) {
            search_start = header_pos;
            break;
        }

        const size_t footer_pos = static_cast<size_t>(footer_it - buffer.begin());
        const size_t packet_end = footer_pos + MAGIC_LENGTH;
        std::vector<uint8_t> packet(buffer.begin() + header_pos, buffer.begin() + packet_end);

        if (packet.size() <= MAX_PACKET_SIZE && validatePacketStructure(packet)) {
            packets.push_back(packet);
            search_start = packet_end;
        } else {
            search_start = header_pos + MAGIC_LENGTH;
        }
    }

    std::vector<uint8_t> remaining(buffer.begin() + std::min(search_start, buffer.size()), buffer.end());
    return {packets, remaining};
}

bool DASProtocol::validatePacketStructure(const std::vector<uint8_t>& packet) {
    if (packet.size() < MAGIC_LENGTH * 2 + 1) {
        return false;
    }
    if (!checkMagic(packet.data(), packet.size())) {
        return false;
    }
    if (!checkMagic(packet.data() + packet.size() - MAGIC_LENGTH, MAGIC_LENGTH)) {
        return false;
    }
    return true;
}

std::optional<DASProtocol::PacketInfo> DASProtocol::parsePacket(const std::vector<uint8_t>& packet) {
    if (!validatePacketStructure(packet)) {
        return std::nullopt;
    }

    PacketInfo info;
    info.raw_packet = packet;
    info.packet_length = packet.size();
    info.timestamp = std::chrono::system_clock::now();

    const size_t content_start = MAGIC_LENGTH;
    const size_t content_end = packet.size() - MAGIC_LENGTH;
    info.opcode = packet[content_start];
    if (content_end > content_start + 1) {
        info.data_section.assign(packet.begin() + content_start + 1, packet.begin() + content_end);
    }
    info.data_length = info.data_section.size();
    return info;
}

std::vector<uint8_t> DASProtocol::createPacket(uint8_t opcode, const std::vector<uint8_t>& data) {
    if (data.size() > 1024) {
        throw std::runtime_error("DAS packet payload too long");
    }

    std::vector<uint8_t> packet;
    packet.reserve(MAGIC_LENGTH * 2 + 1 + data.size());
    packet.insert(packet.end(), MAGIC, MAGIC + MAGIC_LENGTH);
    packet.push_back(opcode);
    packet.insert(packet.end(), data.begin(), data.end());
    packet.insert(packet.end(), MAGIC, MAGIC + MAGIC_LENGTH);
    return packet;
}

} // namespace das
