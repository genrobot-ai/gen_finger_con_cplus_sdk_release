#include <cmath>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera.hpp"
#include "das_protocol.hpp"
#include "databus.hpp"
#include "pack.hpp"
#include "tactile_processing.hpp"

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_cmd_pack_uses_finger_protocol_layout() {
    auto record = das::floatToBigEndianBytes(0.2f);
    das::CmdPack cmd = das::CmdPack::pack(
        das::Opcode::ReadBatch,
        das::RecordType::Encoder,
        record
    );

    const std::vector<uint8_t> magic = {'d', 'a', 's', '\r', '\n'};
    require(cmd.data.size() == 5 + 1 + 1 + 4 + 2 + 2 + 4 + 2 + 5,
            "command packet size must match DAS command layout");
    require(std::equal(magic.begin(), magic.end(), cmd.data.begin()),
            "command packet must start with DAS magic");
    require(std::equal(magic.begin(), magic.end(), cmd.data.end() - 5),
            "command packet must end with DAS magic");
    require(cmd.data[5] == 0x02, "opcode must be ReadBatch");
    require(cmd.data[6] == 0x02, "record type must be Encoder");
    require(cmd.data[7] == 0x00 && cmd.data[8] == 0x00 &&
            cmd.data[9] == 0x00 && cmd.data[10] == 0x04,
            "record length must be big-endian uint32");
    require(cmd.data[13] == 0x00 && cmd.data[14] == 0x50,
            "torque field must default to 80");
}

void test_das_protocol_extracts_complete_packets_and_remainder() {
    std::vector<uint8_t> packet = das::DASProtocol::createPacket(0x04, {'O', 'K'});
    std::vector<uint8_t> buffer = {'x', 'x'};
    buffer.insert(buffer.end(), packet.begin(), packet.end());
    buffer.push_back('z');

    auto [packets, remaining] = das::DASProtocol::findPacket(buffer);

    require(packets.size() == 1, "one complete DAS packet must be found");
    require(packets[0] == packet, "found packet must equal the framed input");
    require(remaining.size() == 1 && remaining[0] == 'z',
            "trailing incomplete bytes must be retained");
}

void test_message_pack_unpacks_big_endian_record_lengths() {
    std::vector<uint8_t> data = {'d', 'a', 's', '\r', '\n'};
    data.push_back(static_cast<uint8_t>(das::Opcode::ReadBatch));
    data.push_back(static_cast<uint8_t>(das::RecordType::Encoder));
    for (int i = 0; i < 7; ++i) data.push_back(0);
    data.push_back(4);
    auto payload = das::floatToBigEndianBytes(0.125f);
    data.insert(data.end(), payload.begin(), payload.end());
    data.insert(data.end(), {'d', 'a', 's', '\r', '\n'});

    auto pack = das::MessagePack::unpack(data);

    require(pack.has_value(), "message pack must parse valid packet");
    require(pack->records.size() == 1, "message pack must contain one record");
    require(pack->records[0].record_type == das::RecordType::Encoder,
            "record type must be Encoder");
    require(pack->records[0].record_data == payload,
            "record payload must be preserved");
}

void test_distance_accepts_finger_range() {
    das::DataBus bus("/dev/null", 921600, 0.5, false, 0, 0,
                     nullptr, nullptr, nullptr, "", "", true);

    bus.setTargetDistance(0.2f);
    require(std::fabs(bus.getTargetDistance() - 0.2f) < 0.0001f,
            "target distance must allow 0.2m for finger devices");

    bool threw = false;
    try {
        bus.setTargetDistance(0.2001f);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "target distance above 0.2m must be rejected");
}

void test_camera_capture_accepts_single_explicit_device_without_three_camera_requirement() {
    das::CameraCapture camera(
        "",
        1,
        {{1600, 1296}},
        false,
        {"/dev/definitely_missing_finger_camera"},
        nullptr,
        30,
        true
    );

    require(camera.getCameras().empty(),
            "missing explicit single camera should not be expanded to three-camera mapping");
    require(!camera.isRunning(),
            "camera capture should stop itself when no explicit camera can be opened");
}

void test_tactile_conversion_shape_and_negative_slots() {
    std::vector<uint8_t> raw(448);
    for (size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<uint8_t>(i % 128);
    }

    auto tactile = das::convert_tactile_448_to_1000(raw);

    require(tactile.first.size() == 500, "left tactile output must contain 500 values");
    require(tactile.second.size() == 500, "right tactile output must contain 500 values");
    require(tactile.first[0] == -1, "left corner invalid slot must be -1");
    require(tactile.second[0] == -1, "right corner invalid slot must be -1");
}

} // namespace

int main() {
    try {
        test_cmd_pack_uses_finger_protocol_layout();
        test_das_protocol_extracts_complete_packets_and_remainder();
        test_message_pack_unpacks_big_endian_record_lengths();
        test_distance_accepts_finger_range();
        test_camera_capture_accepts_single_explicit_device_without_three_camera_requirement();
        test_tactile_conversion_shape_and_negative_slots();
    } catch (const std::exception& e) {
        std::cerr << "TEST FAILED: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "finger_sdk_tests passed" << std::endl;
    return 0;
}
