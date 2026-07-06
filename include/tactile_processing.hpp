/**
 * @file tactile_processing.hpp
 * @brief Tactile packet conversion and optional grid printing.
 */

#ifndef FINGER_TACTILE_PROCESSING_HPP
#define FINGER_TACTILE_PROCESSING_HPP

#include <cstdint>
#include <utility>
#include <vector>

namespace das {

std::pair<std::vector<int>, std::vector<int>>
convert_tactile_448_to_1000(const std::vector<uint8_t>& record_data);

void set_tactile_grid_print_enabled(bool enabled);
void set_tactile_grid_print_max_hz(double max_hz);
void submit_tactile_1000_grid_print(const std::vector<int>& all_tactile);
void print_tactile_1000_grid(const std::vector<int>& all_tactile);

} // namespace das

#endif // FINGER_TACTILE_PROCESSING_HPP
