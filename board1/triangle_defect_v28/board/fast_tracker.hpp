#pragma once

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace triangle_tracking {

struct Track {
    uint64_t id = 0;
    cv::Rect box;
    cv::Point2f center;
    bool partial = false;
    bool ambiguous = false;
};

// Fast display-only localization. Model localization/preprocessing remains on
// the worker's unmodified full-resolution image, independently of this class.
class FastTracker {
    struct State {
        Track track;
        cv::Point2f velocity; // Original-image pixels per observed video frame.
    };
    struct Edge { size_t previous, detection; float cost; };
    int target_width_;
    uint64_t next_id_ = 1;
    cv::Size frame_size_;
    cv::Mat mask_;
    std::array<int, 256> saturation_div_{}, hue_div_{};
    std::vector<int> sample_byte_offsets_;
    int sample_source_width_ = 0;
    cv::Mat kernel_ = cv::Mat::ones(3, 3, CV_8U);
    std::vector<State> previous_;
    std::vector<Track> current_;

    static float length(const cv::Point2f& point) {
        return std::sqrt(point.x * point.x + point.y * point.y);
    }
    static float overlap(const cv::Rect2f& a, const cv::Rect2f& b) {
        const float area = (a & b).area();
        return area / std::max(1.0f, a.area() + b.area() - area);
    }
    static cv::Rect2f predicted(const State& state) {
        return cv::Rect2f(state.track.box.x + state.velocity.x,
                          state.track.box.y + state.velocity.y,
                          float(state.track.box.width), float(state.track.box.height));
    }
    uint64_t newId() {
        if (next_id_ == std::numeric_limits<uint64_t>::max())
            throw std::runtime_error("FastTracker ID space exhausted");
        return next_id_++;
    }

    std::vector<Track> detect(const cv::Mat& bgra) {
        const int width = std::min(target_width_, bgra.cols);
        const int height = std::max(1, int(std::lround(double(bgra.rows) * width / bgra.cols)));
        // Fuse nearest sampling and the exact OpenCV 8-bit HSV blue threshold.
        // H=100..135 requires B to be the unique maximum, so most background
        // pixels need no HSV arithmetic. No small BGRA/BGR/HSV images are made.
        if (sample_source_width_ != bgra.cols || int(sample_byte_offsets_.size()) != width) {
            sample_byte_offsets_.resize(width);
            for (int x = 0; x < width; ++x)
                sample_byte_offsets_[x] = int(int64_t(x) * bgra.cols / width) * 4;
            sample_source_width_ = bgra.cols;
        }
        mask_.create(height, width, CV_8UC1);
        for (int y = 0; y < height; ++y) {
            const auto* source = bgra.ptr<unsigned char>(int(int64_t(y) * bgra.rows / height));
            auto* destination = mask_.ptr<unsigned char>(y);
            for (int x = 0; x < width; ++x) {
                uint32_t pixel;
                std::memcpy(&pixel,source + sample_byte_offsets_[x],sizeof(pixel));
                const int blue = pixel & 255, green = (pixel >> 8) & 255, red = (pixel >> 16) & 255;
                if (blue < 30 || blue <= green || blue <= red) {
                    destination[x] = 0;
                    continue;
                }
                const int difference = blue - std::min(red, green);
                const int saturation = (difference * saturation_div_[blue] + 2048) >> 12;
                if (saturation < 80) {
                    destination[x] = 0;
                    continue;
                }
                const int hue = ((red - green + 4 * difference) * hue_div_[difference] + 2048) >> 12;
                destination[x] = hue >= 100 && hue <= 135 ? 255 : 0;
            }
        }
        cv::morphologyEx(mask_, mask_, cv::MORPH_CLOSE, kernel_);
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(mask_, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        std::vector<Track> found;
        const double xscale = double(bgra.cols) / width, yscale = double(bgra.rows) / height;
        const double min_area = std::max(64.0 / (xscale * yscale), width * double(height) * .001);
        for (const auto& contour : contours) {
            const double area = cv::contourArea(contour);
            const cv::Rect small_box = cv::boundingRect(contour);
            if (area < min_area || small_box.width * xscale < 24 || small_box.height * yscale < 24)
                continue;
            const double aspect = small_box.width * xscale / (small_box.height * yscale);
            if (aspect < .45 || aspect > 2.2 || area / small_box.area() < .5) continue;
            std::vector<cv::Point> hull;
            cv::convexHull(contour, hull);
            if (area / std::max(1.0, cv::contourArea(hull)) < .8) continue;
            const int left = std::max(0, int(std::floor(small_box.x * xscale)));
            const int top = std::max(0, int(std::floor(small_box.y * yscale)));
            const int right = std::min(bgra.cols, int(std::ceil(small_box.br().x * xscale)));
            const int bottom = std::min(bgra.rows, int(std::ceil(small_box.br().y * yscale)));
            Track track;
            track.box = cv::Rect(left, top, right - left, bottom - top);
            track.center = cv::Point2f((left + right) * .5f, (top + bottom) * .5f);
            track.partial = left <= 1 || top <= 1 || right >= bgra.cols - 1 || bottom >= bgra.rows - 1;
            found.push_back(track);
        }
        // Ordering only makes output deterministic; association never uses rank.
        std::sort(found.begin(), found.end(), [](const Track& a, const Track& b) {
            return a.center.x == b.center.x ? a.center.y < b.center.y : a.center.x < b.center.x;
        });
        return found;
    }

    void associate(std::vector<Track> detections) {
        const size_t old_count = previous_.size(), new_count = detections.size();
        std::vector<Edge> edges;
        std::vector<std::vector<size_t>> from_old(old_count), to_new(new_count);
        std::vector<bool> ambiguous_old(old_count, false), ambiguous_new(new_count, false);
        const float frame_diagonal = std::hypot(float(frame_size_.width), float(frame_size_.height));
        for (size_t i = 0; i < old_count; ++i) {
            const auto& state = previous_[i];
            const auto prediction = predicted(state);
            const cv::Point2f expected = state.track.center + state.velocity;
            const float diagonal = std::hypot(float(state.track.box.width), float(state.track.box.height));
            const float gate = std::min(frame_diagonal * .15f,
                std::max(frame_size_.width / 48.0f, diagonal * .7f + length(state.velocity) * 1.5f));
            for (size_t j = 0; j < new_count; ++j) {
                const auto& detection = detections[j];
                const float distance = length(detection.center - expected);
                const float iou = overlap(prediction, cv::Rect2f(detection.box));
                const float area_ratio = float(detection.box.area()) / state.track.box.area();
                const float width_ratio = float(detection.box.width) / state.track.box.width;
                const float height_ratio = float(detection.box.height) / state.track.box.height;
                // A merged blob or abrupt shape change must not inherit a score.
                if (area_ratio < .55f || area_ratio > 1.8f || width_ratio < .6f || width_ratio > 1.67f ||
                    height_ratio < .6f || height_ratio > 1.67f || distance > gate ||
                    (iou < .02f && distance > gate * .65f)) continue;
                const float cost = .65f * (1 - iou) + .35f * distance / gate
                    + .1f * std::abs(std::log(area_ratio));
                from_old[i].push_back(edges.size());
                to_new[j].push_back(edges.size());
                edges.push_back({i, j, cost});
            }
        }

        // Test both sides: a split and a merge are equally unsafe for identity.
        const auto uncertain = [&](std::vector<size_t> candidates) {
            if (candidates.size() < 2) return false;
            std::sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
                return edges[a].cost < edges[b].cost;
            });
            return edges[candidates[1]].cost - edges[candidates[0]].cost < .18f;
        };
        for (size_t i = 0; i < old_count; ++i) {
            if (!uncertain(from_old[i])) continue;
            ambiguous_old[i] = true;
            for (size_t edge : from_old[i]) ambiguous_new[edges[edge].detection] = true;
        }
        for (size_t j = 0; j < new_count; ++j) {
            if (!uncertain(to_new[j])) continue;
            ambiguous_new[j] = true;
            for (size_t edge : to_new[j]) ambiguous_old[edges[edge].previous] = true;
        }

        // Also catch a merger rejected by the area gate: one current bounding
        // box covering substantial portions of two previous predictions.
        for (size_t j = 0; j < new_count; ++j) {
            std::vector<size_t> covered;
            const cv::Rect2f box(detections[j].box);
            for (size_t i = 0; i < old_count; ++i) {
                const auto prediction = predicted(previous_[i]);
                if ((box & prediction).area() / std::max(1.0f, prediction.area()) > .35f)
                    covered.push_back(i);
            }
            if (covered.size() > 1) {
                ambiguous_new[j] = true;
                for (size_t i : covered) ambiguous_old[i] = true;
            }
        }

        std::sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) { return a.cost < b.cost; });
        std::vector<int> matched(new_count, -1);
        std::vector<bool> used(old_count, false);
        for (const auto& edge : edges) {
            if (ambiguous_old[edge.previous] || ambiguous_new[edge.detection] ||
                used[edge.previous] || matched[edge.detection] >= 0) continue;
            matched[edge.detection] = int(edge.previous);
            used[edge.previous] = true;
        }
        std::vector<State> next;
        next.reserve(new_count);
        current_.clear();
        current_.reserve(new_count);
        for (size_t j = 0; j < new_count; ++j) {
            State state;
            state.track = detections[j];
            state.track.ambiguous = ambiguous_new[j];
            if (matched[j] >= 0) {
                const auto& before = previous_[size_t(matched[j])];
                state.track.id = before.track.id;
                state.velocity = (state.track.center - before.track.center) * .7f + before.velocity * .3f;
            } else state.track.id = newId();
            current_.push_back(state.track);
            next.push_back(state);
        }
        // No lost-track pool: a missed frame kills the ID immediately. A later
        // detection receives a new ID and cannot inherit a delayed old result.
        previous_.swap(next);
    }

public:
    explicit FastTracker(int target_width = 480) : target_width_(target_width) {
        if (target_width < 160 || target_width > 640)
            throw std::invalid_argument("FastTracker target width must be between 160 and 640");
        // Same shift and nearest-even lookup rounding as OpenCV's RGB2HSV_b.
        // This predicate was checked against cvtColor+inRange for all 2^24 BGR
        // colors: H=100..135, S=80..255, V=30..255, with zero mask differences.
        for (int value = 1; value < 256; ++value) {
            saturation_div_[value] = cvRound(double(255 << 12) / value);
            hue_div_[value] = cvRound(double(180 << 12) / (6 * value));
        }
    }
    const std::vector<Track>& update(const cv::Mat& bgra) {
        if (bgra.empty() || bgra.type() != CV_8UC4)
            throw std::invalid_argument("FastTracker expects a nonempty BGRA image");
        if (frame_size_ != bgra.size()) {
            reset();
            frame_size_ = bgra.size();
        }
        associate(detect(bgra));
        return current_;
    }
    void reset() {
        previous_.clear();
        current_.clear();
        frame_size_ = cv::Size();
        // Deliberately retain next_id_: stale asynchronous results stay invalid.
    }
};

} // namespace triangle_tracking
