// Precompiled header for Queen Message Queue
// Includes heavy header-only libraries used across most translation units.
// This dramatically reduces compile time by parsing these once instead of
// 30+ times (spdlog in 32/33 files, json.hpp in 27/33 files).
#pragma once

// Heavy third-party header-only libraries
#include <spdlog/spdlog.h>
#include <json.hpp>

// Commonly used STL headers
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <chrono>
#include <atomic>
#include <mutex>
#include <map>
#include <unordered_map>
#include <functional>
