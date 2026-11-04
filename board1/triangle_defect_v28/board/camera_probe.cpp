#include "driver_camera.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
volatile std::sig_atomic_t stopped = 0;

void requestStop(int) { stopped = 1; }

// Keep a stop request pending until the current DMA has completed. This probe
// is single-threaded; no inference runtime or OpenCV worker pool is started.
class CaptureSignalGuard {
    sigset_t previous_{};
public:
    CaptureSignalGuard() {
        sigset_t blocked;
        ::sigemptyset(&blocked);
        ::sigaddset(&blocked, SIGINT);
        ::sigaddset(&blocked, SIGTERM);
        if (::sigprocmask(SIG_BLOCK, &blocked, &previous_) != 0)
            throw std::runtime_error("Cannot block stop signals during capture");
    }
    ~CaptureSignalGuard() { ::sigprocmask(SIG_SETMASK, &previous_, nullptr); }
    CaptureSignalGuard(const CaptureSignalGuard&) = delete;
    CaptureSignalGuard& operator=(const CaptureSignalGuard&) = delete;
};

std::string jsonString(const std::string& value) {
    std::ostringstream result;
    result << '"';
    for (unsigned char character : value) {
        if (character == '"' || character == '\\') result << '\\' << character;
        else if (character < 0x20)
            result << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(character);
        else result << character;
    }
    result << '"';
    return result.str();
}

void report(size_t requested, size_t captured, const std::vector<double>& timings,
            double measured_ms, const std::string& error) {
    std::cout << std::fixed << std::setprecision(3)
              << "{\"requested_frames\":" << requested
              << ",\"captured_frames\":" << captured
              << ",\"warmup_frames\":" << std::min<size_t>(captured, 5)
              << ",\"measured_frames\":" << timings.size()
              << ",\"stopped\":" << (stopped ? "true" : "false")
              << ",\"fps\":";
    if (timings.empty() || measured_ms <= 0) std::cout << "null";
    else std::cout << timings.size() * 1000.0 / measured_ms;
    std::cout << ",\"capture_ms\":";
    if (timings.empty()) std::cout << "null";
    else {
        auto sorted = timings;
        std::sort(sorted.begin(), sorted.end());
        const size_t count = sorted.size();
        const double median = count % 2 ? sorted[count / 2]
            : (sorted[count / 2 - 1] + sorted[count / 2]) * 0.5;
        const size_t p95 = static_cast<size_t>(std::ceil(count * 0.95)) - 1;
        std::cout << "{\"min\":" << sorted.front() << ",\"median\":" << median
                  << ",\"p95\":" << sorted[p95] << ",\"max\":" << sorted.back() << '}';
    }
    if (!error.empty()) std::cout << ",\"error\":" << jsonString(error);
    std::cout << '}' << std::endl;
}
}

int main(int argc, char** argv) {
    size_t requested = 120, captured = 0;
    std::vector<double> timings;
    Clock::time_point measured_start{}, measured_end{};
    std::string error;
    try {
        if (argc > 2) throw std::runtime_error("Usage: camera_probe [total_frames], default 120; first 5 are warmup");
        if (argc == 2) {
            const std::string argument = argv[1];
            size_t consumed = 0;
            if (argument.empty() || argument.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("total_frames must be a positive integer");
            const auto value = std::stoull(argument, &consumed);
            if (!value || consumed != argument.size() || value > std::numeric_limits<size_t>::max())
                throw std::runtime_error("total_frames is outside the supported range");
            requested = static_cast<size_t>(value);
        }
        struct sigaction action{};
        action.sa_handler = requestStop;
        ::sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if (::sigaction(SIGINT, &action, nullptr) != 0 || ::sigaction(SIGTERM, &action, nullptr) != 0)
            throw std::runtime_error("Cannot install stop signal handlers");
        timings.reserve(std::min<size_t>(requested, 4096));
        {
            DriverCamera camera("/dev/mipicam");
            for (size_t frame = 0; frame < requested && !stopped; ++frame) {
                CaptureSignalGuard signal_guard;
                if (stopped) break;
                const auto started = Clock::now();
                const cv::Mat bgra = camera.capture();
                const auto finished = Clock::now();
                ++captured;
                if (frame >= 5) {
                    if (timings.empty()) measured_start = started;
                    measured_end = finished;
                    timings.push_back(std::chrono::duration<double, std::milli>(finished - started).count());
                }
                // The borrowed Mat is discarded before signals are unblocked;
                // no new capture is started after a pending stop is delivered.
            }
        }
    } catch (const std::exception& exception) {
        error = exception.what();
    }
    const double measured_ms = timings.empty() ? 0
        : std::chrono::duration<double, std::milli>(measured_end - measured_start).count();
    report(requested, captured, timings, measured_ms, error);
    return error.empty() ? 0 : 1;
}
