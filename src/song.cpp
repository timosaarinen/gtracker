#include "gtracker/song.hpp"
#include "gtracker/hash.hpp"
#include "gtracker/io.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace gtracker {
namespace {
using json = nlohmann::json;
constexpr std::size_t max_song_bytes = 8 * 1024 * 1024;
void require(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
void keys(const json& object, std::initializer_list<const char*> allowed, const char* where) {
    require(object.is_object(), std::string(where) + " must be an object");
    for (const auto& item : object.items())
        require(std::any_of(allowed.begin(), allowed.end(), [&](const char* key) { return item.key() == key; }),
                std::string(where) + ": unknown field '" + item.key() + "'");
}
double number(const json& value, const char* where) {
    require(value.is_number(), std::string(where) + " must be a number");
    const double result = value.get<double>();
    require(std::isfinite(result), std::string(where) + " must be finite");
    return result;
}
std::uint64_t integer(const json& value, std::uint64_t high, const char* where) {
    require(value.is_number_integer(), std::string(where) + " must be an integer");
    if (value.is_number_unsigned()) {
        const auto result = value.get<std::uint64_t>();
        require(result <= high, std::string(where) + " is out of range");
        return result;
    }
    const auto result = value.get<std::int64_t>();
    require(result >= 0 && static_cast<std::uint64_t>(result) <= high, std::string(where) + " is out of range");
    return static_cast<std::uint64_t>(result);
}
bool same_instrument(const Pattern& a, const Pattern& b) {
    return a.model == b.model && a.tuning_midi == b.tuning_midi && a.decay_seconds == b.decay_seconds &&
           a.brightness == b.brightness && a.body_mix == b.body_mix;
}
std::map<std::string, const Pattern*> pattern_map(const Song& song) {
    std::map<std::string, const Pattern*> result;
    for (const auto& pattern : song.patterns) result.emplace(pattern.id, &pattern.pattern);
    return result;
}
const Pattern& master_pattern(const Song& song) {
    const auto found = std::find_if(song.patterns.begin(), song.patterns.end(), [&](const SongPattern& p) { return p.id == song.instrument_pattern; });
    require(found != song.patterns.end(), "instrument_pattern must reference a declared pattern");
    return found->pattern;
}
void render_options(const RenderOptions& options) {
    require(options.sample_rate >= 8000 && options.sample_rate <= 192000, "sample_rate must be in 8000..192000");
    require(std::isfinite(options.tail_seconds) && options.tail_seconds >= 0 && options.tail_seconds <= 30, "tail must be in 0..30 seconds");
}
}

void validate_song(const Song& s) {
    require(!s.name.empty() && s.name.size() <= 256, "Song name must contain 1..256 UTF-8 bytes");
    require(s.revision > 0 && s.revision <= 9007199254740991ULL, "Song revision must be a positive JSON-safe integer");
    require(std::isfinite(s.bpm) && s.bpm >= 20 && s.bpm <= 400, "Song tempo_bpm must be in 20..400");
    require(!s.patterns.empty() && s.patterns.size() <= 64, "Song requires 1..64 pattern definitions");
    require(!s.order.empty() && s.order.size() <= 4096, "Song requires 1..4096 order entries");
    if (s.production) validate_production(*s.production);
    std::set<std::string> ids;
    std::size_t source_bytes = 0;
    const auto& master = master_pattern(s);
    for (const auto& named : s.patterns) {
        require(!named.id.empty() && named.id.size() <= 128 && ids.insert(named.id).second, "Song pattern IDs must be unique and contain 1..128 bytes");
        validate_pattern(named.pattern);
        require(same_instrument(master, named.pattern), "Song v1 uses one instrument/model; pattern '" + named.id + "' has different settings");
        require(!named.pattern.production, "Per-pattern production is not supported in a song; use song-level production settings");
        source_bytes += pattern_json(named.pattern).size();
        require(source_bytes <= max_song_bytes, "Song's expanded pattern source exceeds 8 MiB");
    }
    const auto patterns = pattern_map(s);
    ids.clear();
    std::size_t instances = 0, events = 0, slides = 0, points = 0;
    double beats = 0;
    for (const auto& instance : s.order) {
        require(!instance.id.empty() && instance.id.size() <= 128 && ids.insert(instance.id).second, "Song instance IDs must be unique and contain 1..128 bytes");
        const auto found = patterns.find(instance.pattern_id);
        require(found != patterns.end(), "Unknown song pattern: " + instance.pattern_id);
        require(instance.repeat >= 1 && instance.repeat <= 256, "Instance repeat must be in 1..256");
        require(std::isfinite(instance.time_scale) && instance.time_scale >= 0.25 && instance.time_scale <= 4, "Instance time_scale must be in 0.25..4");
        require(std::isfinite(instance.transpose) && instance.transpose >= -24 && instance.transpose <= 24, "Instance transpose must be in -24..24 semitones");
        require(std::isfinite(instance.strength_scale) && instance.strength_scale >= 0 && instance.strength_scale <= 2, "Instance strength_scale must be in 0..2");
        require(std::isfinite(instance.gap_beats) && instance.gap_beats >= 0 && instance.gap_beats <= 256, "Instance gap_beats must be in 0..256");
        const auto& p = *found->second;
        instances += static_cast<std::size_t>(instance.repeat);
        require(instances <= 4096, "Song exceeds 4096 expanded pattern instances");
        events += p.events.size() * static_cast<std::size_t>(instance.repeat);
        slides += p.gestures.size() * static_cast<std::size_t>(instance.repeat);
        for (const auto& gesture : p.gestures) {
            points += gesture.points.size() * static_cast<std::size_t>(instance.repeat);
            for (const auto& point : gesture.points) require(point.fret + instance.transpose >= 0 && point.fret + instance.transpose <= 36,
                "Instance transposition puts a slide fret outside 0..36");
        }
        require(events <= 1000000 && slides <= 16384 && points <= 65536, "Expanded song exceeds native event/slide/point limits");
        for (const auto& event : p.events) {
            if (event.type == "pluck") require(event.fret + instance.transpose >= 0 && event.fret + instance.transpose <= 36,
                "Instance transposition puts a pluck fret outside 0..36");
            if (event.type != "damp") require(event.strength * instance.strength_scale <= 1, "Instance strength_scale exceeds the excitation strength limit; lower the scale");
        }
        beats += (p.length_beats * instance.time_scale + instance.gap_beats) * instance.repeat;
        require(std::isfinite(beats) && beats > 0 && beats * (60.0 / s.bpm) <= 86400, "Song musical duration must be positive and at most one day");
    }
}

Song parse_song_json(std::string_view source, const std::filesystem::path& reference_directory) {
    require(source.size() <= max_song_bytes, "Song JSON exceeds 8 MiB");
    const auto doc = json::parse(source);
    keys(doc, {"schema", "version", "name", "revision", "tempo_bpm", "seed", "instrument_pattern", "patterns", "order", "production"}, "song");
    require(doc.at("schema") == "gtracker.song" && integer(doc.at("version"), 1, "song.version") == 1, "Unsupported song schema/version");
    Song s;
    s.name = doc.at("name").get<std::string>();
    s.revision = integer(doc.at("revision"), 9007199254740991ULL, "song.revision");
    s.bpm = number(doc.at("tempo_bpm"), "song.tempo_bpm");
    s.seed = static_cast<std::uint32_t>(integer(doc.at("seed"), std::numeric_limits<std::uint32_t>::max(), "song.seed"));
    s.instrument_pattern = doc.at("instrument_pattern").get<std::string>();
    if (doc.contains("production")) s.production = parse_production_json(doc.at("production").dump());
    const auto& patterns = doc.at("patterns");
    require(patterns.is_array() && !patterns.empty() && patterns.size() <= 64, "Song patterns must contain 1..64 definitions");
    std::size_t expanded_bytes = 0;
    for (const auto& item : patterns) {
        keys(item, {"id", "file", "pattern"}, "song pattern");
        require(item.contains("file") != item.contains("pattern"), "Song pattern needs exactly one of file or embedded pattern");
        SongPattern named; named.id = item.at("id").get<std::string>();
        if (item.contains("file")) {
            named.source_file = item.at("file").get<std::string>();
            const auto file = std::filesystem::u8path(named.source_file);
            require(!reference_directory.empty(), "Referenced song patterns require an explicit reference directory");
            require(!named.source_file.empty() && named.source_file.size() <= 1023 && !file.is_absolute() && !file.has_root_name() &&
                named.source_file.find_first_of("\\:") == std::string::npos &&
                std::none_of(named.source_file.begin(), named.source_file.end(), [](unsigned char c) { return c < 32; }),
                "Song pattern file must be a portable relative UTF-8 path using forward slashes");
            const auto path = reference_directory / file;
            named.pattern = load_pattern(path);
            s.source_files.push_back(std::filesystem::weakly_canonical(path));
        } else named.pattern = parse_pattern_json(item.at("pattern").dump());
        expanded_bytes += pattern_json(named.pattern).size();
        require(expanded_bytes <= max_song_bytes, "Song's expanded pattern source exceeds 8 MiB");
        s.patterns.push_back(std::move(named));
    }
    const auto& order = doc.at("order");
    require(order.is_array() && !order.empty() && order.size() <= 4096, "Song order must contain 1..4096 entries");
    for (const auto& item : order) {
        keys(item, {"id", "pattern", "repeat", "time_scale", "transpose", "strength_scale", "gap_beats"}, "song instance");
        PatternInstance instance;
        instance.id = item.at("id").get<std::string>(); instance.pattern_id = item.at("pattern").get<std::string>();
        instance.repeat = static_cast<int>(integer(item.at("repeat"), 256, "instance.repeat"));
        instance.time_scale = number(item.at("time_scale"), "instance.time_scale");
        instance.transpose = static_cast<float>(number(item.at("transpose"), "instance.transpose"));
        instance.strength_scale = static_cast<float>(number(item.at("strength_scale"), "instance.strength_scale"));
        instance.gap_beats = number(item.at("gap_beats"), "instance.gap_beats");
        s.order.push_back(std::move(instance));
    }
    validate_song(s);
    return s;
}

Song load_song(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    require(size <= max_song_bytes, "Song JSON exceeds 8 MiB");
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "Cannot open song: " + path.u8string());
    std::string source(static_cast<std::size_t>(size), '\0');
    input.read(source.data(), static_cast<std::streamsize>(source.size()));
    require(input.gcount() == static_cast<std::streamsize>(source.size()), "Could not read complete song");
    auto result = parse_song_json(source, path.has_parent_path() ? path.parent_path() : std::filesystem::path("."));
    result.source_files.push_back(std::filesystem::weakly_canonical(path));
    return result;
}

std::string song_snapshot_json(const Song& s) {
    validate_song(s);
    json patterns = json::array(), order = json::array();
    for (const auto& p : s.patterns) patterns.push_back({{"id", p.id}, {"pattern", json::parse(pattern_json(p.pattern))}});
    for (const auto& i : s.order) order.push_back({{"id", i.id}, {"pattern", i.pattern_id}, {"repeat", i.repeat}, {"time_scale", i.time_scale},
        {"transpose", i.transpose}, {"strength_scale", i.strength_scale}, {"gap_beats", i.gap_beats}});
    json result{{"schema", "gtracker.song"}, {"version", 1}, {"name", s.name}, {"revision", s.revision}, {"tempo_bpm", s.bpm},
        {"seed", s.seed}, {"instrument_pattern", s.instrument_pattern}, {"patterns", patterns}, {"order", order}};
    if (s.production) result["production"] = json::parse(production_json(*s.production));
    const auto text = result.dump(2) + "\n";
    require(text.size() <= max_song_bytes, "Expanded song snapshot exceeds 8 MiB");
    return text;
}

CompiledSong compile_song(const Song& s, int rate) {
    validate_song(s);
    require(rate >= 8000 && rate <= 192000, "Song sample rate must be in 8000..192000");
    auto master = master_pattern(s);
    master.events.clear(); master.gestures.clear(); master.seed = s.seed;
    CompiledSong result;
    result.bpm = s.bpm; result.production = s.production;
    result.performance = compile_pattern(master, rate);
    const auto patterns = pattern_map(s);
    const double seconds_per_beat = 60.0 / s.bpm;
    auto rounded_frame = [&](double beat) { return static_cast<std::uint64_t>(std::llround(beat * seconds_per_beat * rate)); };
    auto end_frame_at = [&](double beat) { return static_cast<std::uint64_t>(std::ceil(beat * seconds_per_beat * rate)); };
    double cursor = 0;
    for (const auto& instance : s.order) {
        const auto& p = *patterns.at(instance.pattern_id);
        for (int repeat = 0; repeat < instance.repeat; ++repeat) {
            const double end = cursor + p.length_beats * instance.time_scale;
            result.placements.push_back({instance.id, instance.pattern_id, repeat, cursor, end});
            const auto end_frame = end_frame_at(end);
            const auto first_frame = rounded_frame(cursor);
            require(end_frame > first_frame, "A song placement is shorter than one frame at this sample rate");
            auto frame = [&](double beat) { return rounded_frame(cursor + beat * instance.time_scale); };
            for (const auto& e : p.events) {
                const float fret = e.type == "pluck" ? e.fret + instance.transpose : 0;
                if (e.type == "pluck") require(result.performance.instrument.tuning_hz[e.string_index] * std::exp2(static_cast<double>(fret) / 12) <= rate / 4.0,
                    "Song instance '" + instance.id + "' exceeds this sample rate's pluck pitch limit");
                const float strength = e.type == "damp" ? e.strength : e.strength * instance.strength_scale;
                result.performance.events.push_back({std::min(frame(e.beat), end_frame - 1),
                    e.type == "pluck" ? GT_EVENT_PLUCK : e.type == "damp" ? GT_EVENT_DAMP : GT_EVENT_BODY_HIT,
                    e.type == "body_hit" ? 0 : static_cast<std::uint32_t>(e.string_index), fret, strength});
            }
            for (const auto& g : p.gestures) {
                ScheduledSlide slide; slide.frame = frame(g.beat); slide.string_index = static_cast<std::uint32_t>(g.string_index);
                require(slide.frame < end_frame, "Song slide starts after its placement after sample rounding");
                for (const auto& point : g.points) {
                    require(result.performance.instrument.tuning_hz[g.string_index] * std::exp2(static_cast<double>(point.fret + instance.transpose) / 12) <= rate / 4.0,
                        "Song instance '" + instance.id + "' exceeds this sample rate's slide pitch limit");
                    const auto absolute = frame(g.beat + point.beat);
                    require(absolute <= end_frame && absolute >= slide.frame && (slide.points.empty() || absolute - slide.frame > slide.points.back().frame_offset),
                            "Song slide points collide after sample rounding; separate the points or raise the rate");
                    slide.points.push_back({absolute - slide.frame, point.fret + instance.transpose});
                }
                result.performance.slides.push_back(std::move(slide));
            }
            cursor = end + instance.gap_beats;
        }
    }
    std::stable_sort(result.performance.events.begin(), result.performance.events.end(), [](const gt_event& a, const gt_event& b) { return a.frame < b.frame; });
    std::stable_sort(result.performance.slides.begin(), result.performance.slides.end(), [](const ScheduledSlide& a, const ScheduledSlide& b) { return a.frame < b.frame; });
    result.length_beats = cursor;
    result.musical_frames = end_frame_at(cursor);
    return result;
}

ProductionSettings song_production(const Song& song, const RenderOptions& options) {
    auto settings = options.production_override.value_or(song.production.value_or(ProductionSettings{}));
    validate_production(settings);
    if (options.bypass_production) settings.enabled = false;
    return settings;
}
std::string song_render_identity(const Song& song, const RenderOptions& options) {
    render_options(options);
    json identity{{"schema", "gtracker.song-render-identity"}, {"version", 1}, {"renderer", GT_VERSION}, {"renderer_build", gt_renderer_build_id()},
        {"song", json::parse(song_snapshot_json(song))}, {"sample_rate", options.sample_rate},
        {"instrument_tail_seconds", options.tail_seconds}, {"bypass_production", options.bypass_production}};
    if (options.production_override) identity["production_override"] = json::parse(production_json(*options.production_override));
    return sha256(identity.dump());
}
NativeRenderer song_renderer(const Song& song, const RenderOptions& options) {
    render_options(options);
    auto compiled = compile_song(song, options.sample_rate);
    return compiled_song_renderer(compiled, options);
}
NativeRenderer compiled_song_renderer(const CompiledSong& compiled, const RenderOptions& options) {
    render_options(options);
    require(static_cast<std::uint32_t>(options.sample_rate) == compiled.performance.instrument.sample_rate, "Compiled song rate differs from the requested render rate; recompile it");
    require(std::isfinite(compiled.bpm) && compiled.bpm >= 20 && compiled.bpm <= 400 && std::isfinite(compiled.length_beats) && compiled.length_beats > 0,
            "Compiled song timing is invalid");
    auto settings = options.production_override.value_or(compiled.production.value_or(ProductionSettings{}));
    if (options.bypass_production) settings.enabled = false;
    const double count = std::ceil((compiled.length_beats * (60.0 / compiled.bpm) + options.tail_seconds) * options.sample_rate);
    require(count >= 1 && count <= static_cast<double>(options.sample_rate) * 86400, "Compiled song duration exceeds one day including instrument tail");
    return NativeRenderer(compiled.performance, static_cast<std::uint64_t>(count), settings);
}
StreamRenderResult render_song_wav(const Song& song, const std::filesystem::path& path, const RenderOptions& options, const StreamOptions& stream) {
    require_distinct_output(path, song.source_files);
    auto renderer = song_renderer(song, options);
    return export_wav(renderer, path, song_render_identity(song, options), stream);
}
} // namespace gtracker
