#pragma once
#include "gtracker/export.hpp"

namespace gtracker {
struct CacheProgress {
    std::string phase;
    std::uint64_t bytes = 0, total_bytes = 0;
};
struct CacheOptions {
    std::filesystem::path directory; // Empty disables all cache access.
    bool refresh = false;
    bool read_only = false;
    std::function<void(const CacheProgress&)> progress;
};
struct CachedRenderResult {
    StreamRenderResult render;
    std::string status = "disabled", detail, key;
    double cache_lookup_ms = 0, cache_store_ms = 0;
    double source_render_ms = 0; // Original generation cost from the checked receipt.
};
// Optional local PCM cache. Both hits and misses publish only complete checked
// audio; cancellation before publication retains the caller's previous output.
// A hit skips generation; its reused float metrics come from the checked receipt.
CachedRenderResult cached_export_wav(NativeRenderer& renderer, const std::filesystem::path& output,
                                     const std::string& performance_identity, const StreamOptions& stream = {},
                                     const CacheOptions& cache = {});
} // namespace gtracker
