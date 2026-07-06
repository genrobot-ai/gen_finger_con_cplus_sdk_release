/**
 * @file tactile_processing.cpp
 * @brief Tactile packet conversion and optional grid printing.
 */

#include "tactile_processing.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace das {
namespace {

std::atomic<bool> g_print_enabled{false};
std::atomic<double> g_min_interval_sec{0.0};
std::mutex g_queue_mutex;
std::condition_variable g_queue_cv;
std::queue<std::vector<int>> g_print_queue;
std::unique_ptr<std::thread> g_print_thread;
std::once_flag g_print_thread_once;
bool g_blank_before_next_frame = false;

const std::vector<std::pair<int, int>> LEFT_NEG_COORDS = {
    {0,0},{0,1},{0,2},{1,0},{1,1},{2,0},{0,7},{0,8},{0,9},{1,8},{1,9},{2,9},
    {49,0},{49,1},{49,2},{49,3},{48,0},{48,1},{48,2},{48,3},{47,0},{47,1},{47,2},
    {46,0},{46,1},{46,2},{45,0},{45,1},{45,2},{44,0},{44,1},{43,0},
    {49,6},{49,7},{49,8},{49,9},{48,6},{48,7},{48,8},{48,9},{47,7},{47,8},{47,9},
    {46,7},{46,8},{46,9},{45,7},{45,8},{45,9},{44,8},{44,9},{43,9}
};

const std::vector<std::pair<int, int>> RIGHT_NEG_COORDS = {
    {50,0},{50,1},{50,2},{51,0},{51,1},{52,0},{50,7},{50,8},{50,9},{51,8},{51,9},{52,9},
    {99,0},{99,1},{99,2},{99,3},{98,0},{98,1},{98,2},{98,3},{97,0},{97,1},{97,2},
    {96,0},{96,1},{96,2},{95,0},{95,1},{95,2},{94,0},{94,1},{93,0},
    {99,6},{99,7},{99,8},{99,9},{98,6},{98,7},{98,8},{98,9},{97,7},{97,8},{97,9},
    {96,7},{96,8},{96,9},{95,7},{95,8},{95,9},{94,8},{94,9},{93,9}
};

std::vector<int> duplicate_samples(const std::vector<uint8_t>& values) {
    std::vector<int> expanded;
    expanded.reserve(values.size() * 2);
    for (uint8_t value : values) {
        expanded.push_back(static_cast<int>(value));
        expanded.push_back(static_cast<int>(value));
    }
    return expanded;
}

int to_signed_int8(int value) {
    if (value == -1) return -1;
    return value < 128 ? value : value - 256;
}

std::string build_grid_text(const std::vector<int>& all_tactile) {
    if (all_tactile.size() != 1000) {
        throw std::runtime_error("Expected 1000 tactile values");
    }
    std::ostringstream out;
    if (g_blank_before_next_frame) out << '\n';
    const std::string gap(10, ' ');
    for (int row = 0; row < 50; ++row) {
        int lo = row * 10;
        for (int col = 0; col < 10; ++col) {
            out << std::setw(3) << all_tactile[lo + col];
            if (col < 9) out << ' ';
        }
        out << gap;
        for (int col = 0; col < 10; ++col) {
            out << std::setw(3) << all_tactile[500 + lo + col];
            if (col < 9) out << ' ';
        }
        if (row < 49) out << '\n';
    }
    g_blank_before_next_frame = true;
    return out.str();
}

void print_worker() {
    auto next_deadline = std::chrono::steady_clock::now();
    while (true) {
        std::vector<int> data;
        {
            std::unique_lock<std::mutex> lock(g_queue_mutex);
            g_queue_cv.wait(lock, [] { return !g_print_queue.empty(); });
            data = g_print_queue.back();
            std::queue<std::vector<int>> empty;
            std::swap(g_print_queue, empty);
        }

        double min_interval = g_min_interval_sec.load();
        if (min_interval > 0) {
            auto now = std::chrono::steady_clock::now();
            if (now < next_deadline) {
                std::this_thread::sleep_until(next_deadline);
            }
            next_deadline = std::chrono::steady_clock::now() +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(min_interval));
        }

        if (g_print_enabled.load()) {
            std::cout << build_grid_text(data) << std::endl;
        }
    }
}

void ensure_print_thread() {
    std::call_once(g_print_thread_once, [] {
        g_print_thread = std::make_unique<std::thread>(print_worker);
        g_print_thread->detach();
    });
}

} // namespace

std::pair<std::vector<int>, std::vector<int>>
convert_tactile_448_to_1000(const std::vector<uint8_t>& record_data) {
    if (record_data.size() != 448) {
        throw std::runtime_error("Expected 448 tactile bytes");
    }

    std::vector<uint8_t> raw_left(record_data.begin(), record_data.begin() + 224);
    std::vector<uint8_t> raw_right(record_data.begin() + 224, record_data.end());
    std::vector<int> left_expanded = duplicate_samples(raw_left);
    std::vector<int> right_expanded = duplicate_samples(raw_right);

    std::vector<std::vector<int>> total_grid(100, std::vector<int>(10, 0));
    for (const auto& [row, col] : LEFT_NEG_COORDS) total_grid[row][col] = -1;
    for (const auto& [row, col] : RIGHT_NEG_COORDS) total_grid[row][col] = -1;

    size_t left_idx = 0;
    for (int row = 0; row < 50; ++row) {
        for (int col = 0; col < 10; ++col) {
            if (total_grid[row][col] != -1 && left_idx < left_expanded.size()) {
                total_grid[row][col] = left_expanded[left_idx++];
            }
        }
    }

    size_t right_idx = 0;
    for (int row = 50; row < 100; ++row) {
        for (int col = 0; col < 10; ++col) {
            if (total_grid[row][col] != -1 && right_idx < right_expanded.size()) {
                total_grid[row][col] = right_expanded[right_idx++];
            }
        }
    }

    std::vector<int> left_flat;
    std::vector<int> right_flat;
    left_flat.reserve(500);
    right_flat.reserve(500);
    for (int row = 0; row < 50; ++row) {
        for (int col = 0; col < 10; ++col) left_flat.push_back(to_signed_int8(total_grid[row][col]));
    }
    for (int row = 50; row < 100; ++row) {
        for (int col = 0; col < 10; ++col) right_flat.push_back(to_signed_int8(total_grid[row][col]));
    }
    return {left_flat, right_flat};
}

void set_tactile_grid_print_enabled(bool enabled) {
    g_print_enabled = enabled;
    if (!enabled) {
        g_blank_before_next_frame = false;
    }
}

void set_tactile_grid_print_max_hz(double max_hz) {
    g_min_interval_sec = max_hz <= 0 ? 0.0 : 1.0 / max_hz;
}

void submit_tactile_1000_grid_print(const std::vector<int>& all_tactile) {
    if (!g_print_enabled.load() || all_tactile.size() != 1000) return;
    ensure_print_thread();
    {
        std::lock_guard<std::mutex> lock(g_queue_mutex);
        std::queue<std::vector<int>> empty;
        std::swap(g_print_queue, empty);
        g_print_queue.push(all_tactile);
    }
    g_queue_cv.notify_one();
}

void print_tactile_1000_grid(const std::vector<int>& all_tactile) {
    if (!g_print_enabled.load()) return;
    std::cout << build_grid_text(all_tactile) << std::endl;
}

} // namespace das
