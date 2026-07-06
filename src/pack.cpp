/**
 * @file pack.cpp
 * @brief DAS command packing and message parsing implementation.
 */

#include "pack.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace das {

std::vector<uint8_t> Record::pack() const {
    std::vector<uint8_t> packet;
    packet.push_back(static_cast<uint8_t>(record_type));

    uint64_t len = record_content_length;
    for (int i = 0; i < 8; ++i) {
        packet.push_back(static_cast<uint8_t>(len & 0xFF));
        len >>= 8;
    }

    packet.insert(packet.end(), record_data.begin(), record_data.end());
    return packet;
}

CmdPack::CmdPack(const std::vector<uint8_t>& data,
                 std::optional<Opcode> opcode,
                 std::optional<RecordType> record_type,
                 const std::vector<uint8_t>& record_data)
    : data(data), opcode(opcode), record_type(record_type), record_data(record_data) {}

CmdPack CmdPack::pack(Opcode opcode, RecordType record_type, const std::vector<uint8_t>& record) {
    std::vector<uint8_t> packet;
    packet.insert(packet.end(), PackContent::MAGIC, PackContent::MAGIC + PackContent::MAGIC_LENGTH);
    packet.push_back(static_cast<uint8_t>(opcode));
    packet.push_back(static_cast<uint8_t>(record_type));

    uint32_t len = static_cast<uint32_t>(record.size());
    packet.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
    packet.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
    packet.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(len & 0xFF));

    packet.push_back(0);
    packet.push_back(0);

    constexpr uint16_t torque = 80;
    packet.push_back(static_cast<uint8_t>((torque >> 8) & 0xFF));
    packet.push_back(static_cast<uint8_t>(torque & 0xFF));

    packet.insert(packet.end(), record.begin(), record.end());
    packet.push_back(0);
    packet.push_back(1);
    packet.insert(packet.end(), PackContent::MAGIC, PackContent::MAGIC + PackContent::MAGIC_LENGTH);

    return CmdPack(packet, opcode, record_type, record);
}

CmdPack CmdPack::packCalib(const std::vector<uint8_t>& record) {
    return CmdPack(record, std::nullopt, std::nullopt, record);
}

std::optional<CmdPack> CmdPack::unpack(const std::vector<uint8_t>& data) {
    if (data.size() < PackContent::MAGIC_LENGTH * 2 + 10) {
        return std::nullopt;
    }
    if (std::memcmp(data.data(), PackContent::MAGIC, PackContent::MAGIC_LENGTH) != 0) {
        return std::nullopt;
    }
    if (std::memcmp(data.data() + data.size() - PackContent::MAGIC_LENGTH,
                    PackContent::MAGIC, PackContent::MAGIC_LENGTH) != 0) {
        return std::nullopt;
    }

    const size_t opcode_pos = PackContent::MAGIC_LENGTH;
    const size_t record_type_pos = opcode_pos + 1;
    const size_t length_pos = record_type_pos + 1;
    const size_t record_start = length_pos + 8;
    const size_t record_end = data.size() - PackContent::MAGIC_LENGTH;
    if (record_start > record_end) {
        return std::nullopt;
    }

    uint64_t record_content_length = 0;
    for (int i = 0; i < 8; ++i) {
        record_content_length |= static_cast<uint64_t>(data[length_pos + i]) << (i * 8);
    }

    std::vector<uint8_t> record_data(data.begin() + record_start, data.begin() + record_end);
    if (record_data.size() != record_content_length) {
        return std::nullopt;
    }

    return CmdPack(data,
                   static_cast<Opcode>(data[opcode_pos]),
                   static_cast<RecordType>(data[record_type_pos]),
                   record_data);
}

MessagePack::MessagePack(const std::vector<uint8_t>& data,
                         std::optional<Opcode> opcode,
                         const std::vector<Record>& records)
    : data(data), opcode(opcode), records(records) {}

bool MessagePack::checkMagic(const uint8_t* data, size_t length) {
    return length >= PackContent::MAGIC_LENGTH &&
           std::memcmp(data, PackContent::MAGIC, PackContent::MAGIC_LENGTH) == 0;
}

bool MessagePack::checkHead(const std::vector<uint8_t>& data) {
    return !data.empty() && checkMagic(data.data(), data.size());
}

bool MessagePack::checkTail(const std::vector<uint8_t>& data) {
    return data.size() >= PackContent::MAGIC_LENGTH &&
           checkMagic(data.data() + data.size() - PackContent::MAGIC_LENGTH, PackContent::MAGIC_LENGTH);
}

std::vector<uint8_t> MessagePack::pack(Opcode opcode, const std::vector<Record>& records) {
    std::vector<uint8_t> packet;
    packet.insert(packet.end(), PackContent::MAGIC, PackContent::MAGIC + PackContent::MAGIC_LENGTH);
    packet.push_back(static_cast<uint8_t>(opcode));
    for (const auto& record : records) {
        auto bytes = record.pack();
        packet.insert(packet.end(), bytes.begin(), bytes.end());
    }
    packet.insert(packet.end(), PackContent::MAGIC, PackContent::MAGIC + PackContent::MAGIC_LENGTH);
    return packet;
}

std::optional<MessagePack> MessagePack::unpack(const std::vector<uint8_t>& data) {
    try {
        if (!checkHead(data) || !checkTail(data)) {
            return std::nullopt;
        }

        std::vector<Record> records;
        const size_t opcode_pos = PackContent::MAGIC_LENGTH;
        size_t i = opcode_pos + 1;
        const uint8_t opcode_value = data[opcode_pos];
        const size_t data_end = data.size() - PackContent::MAGIC_LENGTH;

        while (i < data_end) {
            const uint8_t record_type_value = data[i];
            const size_t length_pos = i + 1;
            if (length_pos + 8 > data_end) {
                return std::nullopt;
            }

            uint64_t record_content_length = 0;
            for (int j = 0; j < 8; ++j) {
                record_content_length = (record_content_length << 8) | data[length_pos + j];
            }

            const size_t record_start = length_pos + 8;
            const size_t record_end = record_start + record_content_length;
            if (record_end > data_end) {
                return std::nullopt;
            }

            std::vector<uint8_t> record_data(data.begin() + record_start, data.begin() + record_end);
            records.emplace_back(static_cast<RecordType>(record_type_value), record_data);
            i = record_end;
        }

        return MessagePack(data, static_cast<Opcode>(opcode_value), records);
    } catch (...) {
        return std::nullopt;
    }
}

std::pair<uint64_t, size_t> MessagePack::readVarint(const std::vector<uint8_t>& data, size_t pos) {
    uint64_t result = 0;
    int shift = 0;
    while (true) {
        if (pos >= data.size()) {
            throw std::runtime_error("varint overflow");
        }
        uint8_t b = data[pos++];
        result |= static_cast<uint64_t>(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            return {result, pos};
        }
        shift += 7;
        if (shift >= 70) {
            throw std::runtime_error("varint too long");
        }
    }
}

MessagePack::CalibInfo MessagePack::parseProtobufCalib(const std::vector<uint8_t>& data) {
    CalibInfo info;
    size_t pos = 0;
    try {
        while (pos < data.size()) {
            auto [tag, next_pos] = readVarint(data, pos);
            pos = next_pos;
            uint64_t field_num = tag >> 3;
            uint64_t wire_type = tag & 0x07;

            if (field_num == 2 && wire_type == 5 && pos + 4 <= data.size()) {
                std::memcpy(&info.width, &data[pos], 4);
                pos += 4;
            } else if (field_num == 3 && wire_type == 5 && pos + 4 <= data.size()) {
                std::memcpy(&info.height, &data[pos], 4);
                pos += 4;
            } else if (field_num == 4 && wire_type == 2) {
                auto [length, p] = readVarint(data, pos);
                pos = p;
                if (pos + length > data.size()) break;
                info.model.assign(data.begin() + pos, data.begin() + pos + length);
                pos += length;
            } else if ((field_num == 5 || field_num == 6 || field_num == 10) && wire_type == 2) {
                auto [length, p] = readVarint(data, pos);
                pos = p;
                if (pos + length > data.size()) break;
                size_t count = length / 8;
                std::vector<double> values;
                values.reserve(count);
                for (size_t i = 0; i < count; ++i) {
                    double v = 0.0;
                    std::memcpy(&v, &data[pos + i * 8], 8);
                    values.push_back(v);
                }
                if (field_num == 5) info.distortion = values;
                if (field_num == 6) info.intrinsics = values;
                if (field_num == 10) info.extrinsics = values;
                pos += length;
            } else {
                if (wire_type == 0) {
                    auto [_, p] = readVarint(data, pos);
                    (void)_;
                    pos = p;
                } else if (wire_type == 1) {
                    pos += 8;
                } else if (wire_type == 2) {
                    auto [length, p] = readVarint(data, pos);
                    pos = p + length;
                } else if (wire_type == 5) {
                    pos += 4;
                } else {
                    break;
                }
            }
        }
    } catch (...) {
    }
    return info;
}

std::string MessagePack::generateYaml(const CalibInfo& info) {
    std::ostringstream yaml;
    std::string model = (info.model == "equidistant") ? "kb4" : info.model;

    std::vector<double> intrinsics;
    if (info.intrinsics.size() >= 6) {
        intrinsics = {info.intrinsics[0], info.intrinsics[4], info.intrinsics[2], info.intrinsics[5]};
    }

    std::vector<double> t_bs_data = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    if (info.extrinsics.size() >= 7) {
        const double tx = info.extrinsics[0], ty = info.extrinsics[1], tz = info.extrinsics[2];
        const double qx = info.extrinsics[3], qy = info.extrinsics[4];
        const double qz = info.extrinsics[5], qw = info.extrinsics[6];
        const double xx = qx * qx, yy = qy * qy, zz = qz * qz;
        const double xy = qx * qy, xz = qx * qz, yz = qy * qz;
        const double wx = qw * qx, wy = qw * qy, wz = qw * qz;
        t_bs_data = {
            1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy), tx,
            2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx), ty,
            2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy), tz,
            0.0, 0.0, 0.0, 1.0
        };
    }

    yaml << "# General sensor definitions.\n";
    yaml << "sensor_type: camera\n";
    yaml << "comment: DAS Camera cam0\n\n";
    yaml << "# Sensor extrinsics wrt. the body-frame.\n";
    yaml << "T_BS:\n  cols: 4\n  rows: 4\n  data: [";
    for (size_t i = 0; i < t_bs_data.size(); ++i) {
        yaml << t_bs_data[i] << (i + 1 < t_bs_data.size() ? ", " : "");
    }
    yaml << "]\n\n";
    yaml << "# Camera specific definitions.\n";
    yaml << "rate_hz: 30\n";
    yaml << "resolution: [" << info.width << ", " << info.height << "]\n";
    yaml << "camera_model: " << model << "\n";
    yaml << "intrinsics: [";
    for (size_t i = 0; i < intrinsics.size(); ++i) {
        yaml << intrinsics[i] << (i + 1 < intrinsics.size() ? ", " : "");
    }
    yaml << "] #fu, fv, cu, cv\n";
    yaml << "distortion_model: " << model << "\n";
    yaml << "distortion_coefficients: [";
    for (size_t i = 0; i < info.distortion.size(); ++i) {
        yaml << info.distortion[i] << (i + 1 < info.distortion.size() ? ", " : "");
    }
    yaml << "] #k1, k2, k3, k4\n";
    return yaml.str();
}

bool MessagePack::unpackCameraCalib(const std::vector<uint8_t>& data,
                                    const std::string& yaml_filename,
                                    const std::string& output_dir,
                                    const std::string& calib_cmd_name) {
    try {
        if (!checkHead(data) || !checkTail(data)) {
            return false;
        }
        if (data.size() < PackContent::MAGIC_LENGTH + 3 + PackContent::MAGIC_LENGTH) {
            return false;
        }

        const size_t length_pos = PackContent::MAGIC_LENGTH;
        const uint16_t payload_length = (static_cast<uint16_t>(data[length_pos]) << 8) |
                                        static_cast<uint16_t>(data[length_pos + 1]);
        (void)payload_length;

        const size_t payload_start = length_pos + 3;
        std::vector<uint8_t> payload(data.begin() + payload_start,
                                     data.end() - PackContent::MAGIC_LENGTH);

        if (!yaml_filename.empty()) {
            CalibInfo info = parseProtobufCalib(payload);
            const std::string yaml_content = generateYaml(info);
            std::cout << "Generated YAML content: " << yaml_content << std::endl;

            std::filesystem::create_directories(output_dir.empty() ? "calib_result" : output_dir);
            const std::string result_dir = output_dir.empty() ? "calib_result" : output_dir;
            const std::string file_path = result_dir + "/" + yaml_filename;
            std::ofstream file(file_path);
            if (!file.is_open()) {
                std::cerr << "Failed to save calibration file: " << file_path << std::endl;
                return false;
            }
            file << yaml_content;
            std::cout << "Camera calibration YAML saved: " << file_path << std::endl;
            return true;
        }

        const std::string prefix = calib_cmd_name.empty()
            ? "Device response"
            : "Device response (" + calib_cmd_name + ")";
        std::string payload_text(payload.begin(), payload.end());
        std::cout << prefix << ": " << payload_text << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Parse calibration data error: " << e.what() << std::endl;
        return false;
    }
}

} // namespace das
