#include "gtracker/project.hpp"
#include "gtracker/core.h"
#include "gtracker/hash.hpp"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace gtracker {
namespace {
using json = nlohmann::json;
constexpr std::size_t max_document_bytes = 1024 * 1024;
constexpr std::size_t max_render_samples = 32 * 1024 * 1024;

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void keys(const json& object, std::initializer_list<const char*> allowed, const char* where) {
    require(object.is_object(), std::string(where) + " must be an object");
    for (const auto& item : object.items()) {
        const bool known = std::any_of(allowed.begin(), allowed.end(), [&](const char* key) { return item.key() == key; });
        require(known, std::string(where) + ": unknown field '" + item.key() + "'");
    }
}

double real_number(const json& value, const char* where) {
    // nlohmann arithmetic conversion also accepts bool; the musical schema does not.
    require(value.is_number(), std::string(where) + " must be a number, not a boolean or string");
    const double result = value.get<double>();
    require(std::isfinite(result), std::string(where) + " must be finite");
    return result;
}

std::uint64_t unsigned_integer(const json& value, std::uint64_t maximum, const char* where) {
    require(value.is_number_integer(), std::string(where) + " must be an integer");
    if (value.is_number_unsigned()) {
        const auto n = value.get<std::uint64_t>();
        require(n <= maximum, std::string(where) + " is out of range");
        return n;
    }
    const auto n = value.get<std::int64_t>();
    require(n >= 0 && static_cast<std::uint64_t>(n) <= maximum, std::string(where) + " is out of range");
    return static_cast<std::uint64_t>(n);
}

std::filesystem::path temporary_path(const std::filesystem::path& destination) {
    static std::atomic<unsigned long long> counter{0};
    if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path());
    auto result = destination;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    result += ".tmp-" + std::to_string(stamp) + "-" + std::to_string(counter++);
    return result;
}

void publish_file(const std::filesystem::path& temporary, const std::filesystem::path& destination) {
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const auto error = GetLastError();
        throw std::runtime_error("Cannot replace output file (Windows error " + std::to_string(error) + ")");
    }
#else
    std::filesystem::rename(temporary, destination);
#endif
}

template<typename Writer> void write_file(const std::filesystem::path& path, Writer writer) {
    require(!path.empty(), "Output path must not be empty");
    const auto temporary = temporary_path(path);
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        require(static_cast<bool>(out), "Cannot create output: " + path.u8string());
        writer(out);
        out.flush();
        require(static_cast<bool>(out), "Cannot write output: " + path.u8string());
        out.close();
        require(static_cast<bool>(out), "Cannot close output: " + path.u8string());
        publish_file(temporary, path);
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

void check_core(gt_result result) {
    if (result != GT_OK) throw std::runtime_error(std::string("Synthesizer: ") + gt_result_string(result));
}

json production_value(const ProductionSettings& s) {
    validate_production(s);
    return {{"schema", "gtracker.production"}, {"version", s.version}, {"algorithm", s.algorithm},
        {"enabled", s.enabled}, {"wet", s.wet}, {"decay_seconds", s.decay_seconds}, {"damping", s.damping},
        {"predelay_ms", s.predelay_ms}, {"width", s.width}, {"output_gain_db", s.output_gain_db}};
}

ProductionSettings read_production(const json& doc) {
    keys(doc, {"schema", "version", "algorithm", "enabled", "wet", "decay_seconds", "damping", "predelay_ms", "width", "output_gain_db"}, "production");
    require(doc.at("schema") == "gtracker.production", "Unsupported production schema");
    ProductionSettings s;
    s.version = static_cast<int>(unsigned_integer(doc.at("version"), 1, "production.version"));
    s.algorithm = doc.at("algorithm").get<std::string>();
    require(doc.at("enabled").is_boolean(), "production.enabled must be boolean");
    s.enabled = doc.at("enabled").get<bool>();
    s.wet = static_cast<float>(real_number(doc.at("wet"), "production.wet"));
    s.decay_seconds = static_cast<float>(real_number(doc.at("decay_seconds"), "production.decay_seconds"));
    s.damping = static_cast<float>(real_number(doc.at("damping"), "production.damping"));
    s.predelay_ms = static_cast<float>(real_number(doc.at("predelay_ms"), "production.predelay_ms"));
    s.width = static_cast<float>(real_number(doc.at("width"), "production.width"));
    s.output_gain_db = static_cast<float>(real_number(doc.at("output_gain_db"), "production.output_gain_db"));
    validate_production(s);
    return s;
}
}

ProductionSettings parse_production_json(std::string_view source) {
    require(source.size() <= 65536, "Production settings exceed 64 KiB");
    return read_production(json::parse(source));
}
std::string production_json(const ProductionSettings& settings) { return production_value(settings).dump(2) + "\n"; }

void validate_pattern(const Pattern& p) {
    require(p.schema_version == 1 || p.schema_version == 2, "Unsupported schema_version; expected 1 or 2");
    require(p.model == "baseline_0_1" || p.model == "contact_0_2", "Unsupported instrument model: " + p.model);
    require(p.schema_version == 2 || (p.model == "baseline_0_1" && p.gestures.empty()), "Schema v1 supports only baseline_0_1 with no gestures; explicitly select v2/contact_0_2");
    require(p.gestures.empty() || p.model == "contact_0_2", "Slide gestures require the explicit contact_0_2 model");
    require(p.schema_version == 2 || !p.production, "Production settings require explicit pattern v2");
    if (p.production) validate_production(*p.production);
    require(!p.name.empty() && p.name.size() <= 256, "name must contain 1..256 UTF-8 bytes");
    require(p.revision > 0 && p.revision <= 9007199254740991ULL, "revision must be a positive JSON-safe integer");
    require(std::isfinite(p.bpm) && p.bpm >= 20 && p.bpm <= 400, "tempo_bpm must be in 20..400");
    require(std::isfinite(p.length_beats) && p.length_beats > 0 && p.length_beats <= 256, "length_beats must be in (0, 256]");
    require(!p.tuning_midi.empty() && p.tuning_midi.size() <= 12, "instrument requires 1..12 independent strings");
    for (double pitch : p.tuning_midi)
        require(std::isfinite(pitch) && pitch >= 16 && pitch <= 95, "tuning_midi pitches must be in 16..95");
    require(std::isfinite(p.decay_seconds) && p.decay_seconds >= 0.02f && p.decay_seconds <= 60, "decay_seconds must be in 0.02..60");
    require(std::isfinite(p.brightness) && p.brightness >= 0 && p.brightness <= 1, "brightness must be in 0..1");
    require(std::isfinite(p.body_mix) && p.body_mix >= 0 && p.body_mix <= 1, "body_mix must be in 0..1");
    require(p.events.size() <= 16384, "Prototype pattern limit is 16384 events");
    std::set<std::string> ids;
    for (const auto& event : p.events) {
        require(!event.id.empty() && event.id.size() <= 128 && ids.insert(event.id).second, "Event IDs must be unique and contain 1..128 bytes");
        require(event.type == "pluck" || event.type == "damp" || event.type == "body_hit", "Unsupported event type: " + event.type);
        require(std::isfinite(event.beat) && event.beat >= 0 && event.beat < p.length_beats, "Event beat must be inside the pattern");
        require(std::isfinite(event.fret) && event.fret >= 0 && event.fret <= 36, "Event fret must be in 0..36");
        require(std::isfinite(event.strength) && event.strength >= 0 && event.strength <= 1, "Event strength must be in 0..1");
        if (event.type != "body_hit")
            require(event.string_index >= 0 && static_cast<std::size_t>(event.string_index) < p.tuning_midi.size(), "Event string is outside the instrument");
    }
    require(p.gestures.size() <= 16384, "Pattern limit is 16384 gestures");
    std::size_t total_points = 0;
    std::vector<const Gesture*> ordered;
    std::vector<const Event*> plucks;
    for (const auto& event : p.events) if (event.type == "pluck") plucks.push_back(&event);
    std::stable_sort(plucks.begin(), plucks.end(), [](const Event* a, const Event* b) { return a->beat < b->beat; });
    for (const auto& gesture : p.gestures) {
        require(!gesture.id.empty() && gesture.id.size() <= 128 && ids.insert(gesture.id).second, "Event/gesture IDs must be unique and contain 1..128 bytes");
        require(gesture.type == "slide", "Unsupported gesture type: " + gesture.type + "; only slide is implemented");
        require(std::isfinite(gesture.beat) && gesture.beat >= 0 && gesture.beat < p.length_beats, "Gesture start must be inside the pattern");
        require(gesture.string_index >= 0 && static_cast<std::size_t>(gesture.string_index) < p.tuning_midi.size(), "Gesture string is outside the instrument");
        require(gesture.points.size() >= 2 && gesture.points.size() <= 256, "Slide requires 2..256 fret points");
        require(total_points <= 65536 - gesture.points.size(), "Pattern exceeds 65536 fret points");
        total_points += gesture.points.size();
        require(gesture.points.front().beat == 0, "Slide's first point must have beat offset zero");
        double previous = -1;
        for (const auto& point : gesture.points) {
            require(std::isfinite(point.beat) && point.beat > previous && point.beat <= 256, "Slide point offsets must be finite, nonnegative and strictly increasing");
            require(std::isfinite(point.fret) && point.fret >= 0 && point.fret <= 36, "Slide fret must be in 0..36");
            previous = point.beat;
        }
        const double end = gesture.beat + gesture.points.back().beat;
        require(end <= p.length_beats + 1e-9, "Slide ends beyond the pattern");
        const auto first = std::lower_bound(plucks.begin(), plucks.end(), gesture.beat, [](const Event* event, double beat) { return event->beat < beat; });
        for (auto event = first; event != plucks.end() && (*event)->beat < end - 1e-9; ++event) {
            if ((*event)->string_index == gesture.string_index)
                require(std::abs((*event)->beat - gesture.beat) <= 1e-9 && (*event)->fret == gesture.points.front().fret,
                        "Slide '" + gesture.id + "' conflicts with pluck '" + (*event)->id + "'; use its initial fret at the same start, or pluck after the slide");
        }
        ordered.push_back(&gesture);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const Gesture* a, const Gesture* b) { return a->beat < b->beat; });
    std::vector<double> ends(p.tuning_midi.size(), -1);
    for (const auto* gesture : ordered) {
        auto& end = ends[static_cast<std::size_t>(gesture->string_index)];
        require(gesture->beat >= end - 1e-9, "Same-string slides cannot overlap");
        end = gesture->beat + gesture->points.back().beat;
    }
}

Pattern parse_pattern_json(std::string_view source) {
    require(source.size() <= max_document_bytes, "Pattern JSON exceeds the 1 MiB prototype limit");
    const auto doc = json::parse(source);
    require(doc.is_object(), "Pattern JSON must be an object");
    const auto version = unsigned_integer(doc.at("schema_version"), 9007199254740991ULL, "schema_version");
    require(version == 1 || version == 2, "Unsupported schema_version; expected 1 or 2");
    if (version == 1) keys(doc, {"schema_version", "name", "revision", "tempo_bpm", "length_beats", "seed", "instrument", "events"}, "pattern");
    else keys(doc, {"schema_version", "name", "revision", "tempo_bpm", "length_beats", "seed", "instrument", "events", "gestures", "production"}, "pattern v2");
    Pattern p;
    p.schema_version = static_cast<int>(version);
    if (version == 2 && doc.contains("production")) p.production = read_production(doc.at("production"));
    p.name = doc.at("name").get<std::string>();
    p.revision = unsigned_integer(doc.at("revision"), 9007199254740991ULL, "revision");
    p.bpm = real_number(doc.at("tempo_bpm"), "tempo_bpm");
    p.length_beats = real_number(doc.at("length_beats"), "length_beats");
    p.seed = static_cast<std::uint32_t>(unsigned_integer(doc.at("seed"), std::numeric_limits<std::uint32_t>::max(), "seed"));
    const auto& instrument = doc.at("instrument");
    if (version == 1) keys(instrument, {"tuning_midi", "decay_seconds", "brightness", "body_mix"}, "instrument");
    else {
        keys(instrument, {"tuning_midi", "decay_seconds", "brightness", "body_mix", "model"}, "instrument v2");
        p.model = instrument.at("model").get<std::string>();
    }
    const auto& tuning = instrument.at("tuning_midi");
    require(tuning.is_array() && !tuning.empty() && tuning.size() <= 12, "tuning_midi must contain 1..12 pitches");
    p.tuning_midi.clear();
    for (const auto& pitch : tuning) p.tuning_midi.push_back(real_number(pitch, "tuning_midi pitch"));
    p.decay_seconds = static_cast<float>(real_number(instrument.at("decay_seconds"), "decay_seconds"));
    p.brightness = static_cast<float>(real_number(instrument.at("brightness"), "brightness"));
    p.body_mix = static_cast<float>(real_number(instrument.at("body_mix"), "body_mix"));
    const auto& events = doc.at("events");
    require(events.is_array() && events.size() <= 16384, "events must be an array with at most 16384 elements");
    for (const auto& item : events) {
        keys(item, {"id", "type", "beat", "string", "fret", "strength"}, "event");
        Event event;
        event.id = item.at("id").get<std::string>();
        event.type = item.at("type").get<std::string>();
        event.beat = real_number(item.at("beat"), "event.beat");
        event.strength = static_cast<float>(real_number(item.at("strength"), "event.strength"));
        if (event.type == "body_hit") {
            require(!item.contains("string") && !item.contains("fret"), "body_hit has no string or fret");
        } else {
            const auto string = unsigned_integer(item.at("string"), 12, "event.string");
            require(string > 0, "JSON string numbers begin at 1 (highest string)");
            event.string_index = static_cast<int>(string - 1);
            if (event.type == "pluck") event.fret = static_cast<float>(real_number(item.at("fret"), "event.fret"));
            else require(!item.contains("fret"), "damp has no fret");
        }
        p.events.push_back(event);
    }
    if (version == 2) {
        const auto& gestures = doc.at("gestures");
        require(gestures.is_array() && gestures.size() <= 16384, "gestures must be an array of at most 16384 slides");
        for (const auto& item : gestures) {
            keys(item, {"id", "type", "beat", "string", "points"}, "gesture");
            Gesture gesture;
            gesture.id = item.at("id").get<std::string>();
            gesture.type = item.at("type").get<std::string>();
            require(gesture.type == "slide", "Unsupported gesture type: " + gesture.type + "; only slide is implemented");
            gesture.beat = real_number(item.at("beat"), "gesture.beat");
            const auto string = unsigned_integer(item.at("string"), 12, "gesture.string");
            require(string > 0, "JSON gesture string numbers begin at 1");
            gesture.string_index = static_cast<int>(string - 1);
            const auto& points = item.at("points");
            require(points.is_array() && points.size() >= 2 && points.size() <= 256, "Slide requires 2..256 points");
            for (const auto& point : points) {
                keys(point, {"beat", "fret"}, "slide point");
                gesture.points.push_back({real_number(point.at("beat"), "slide point.beat"), static_cast<float>(real_number(point.at("fret"), "slide point.fret"))});
            }
            p.gestures.push_back(std::move(gesture));
        }
    }
    validate_pattern(p);
    return p;
}

std::string pattern_json(const Pattern& p) {
    validate_pattern(p);
    json events = json::array();
    for (const auto& event : p.events) {
        json item{{"id", event.id}, {"type", event.type}, {"beat", event.beat}, {"strength", event.strength}};
        if (event.type != "body_hit") item["string"] = event.string_index + 1;
        if (event.type == "pluck") item["fret"] = event.fret;
        events.push_back(std::move(item));
    }
    json doc{{"schema_version", p.schema_version}, {"name", p.name}, {"revision", p.revision},
        {"tempo_bpm", p.bpm}, {"length_beats", p.length_beats}, {"seed", p.seed},
        {"instrument", {{"tuning_midi", p.tuning_midi}, {"decay_seconds", p.decay_seconds},
                        {"brightness", p.brightness}, {"body_mix", p.body_mix}}},
        {"events", events}};
    if (p.schema_version == 2) {
        if (p.production) doc["production"] = production_value(*p.production);
        doc["instrument"]["model"] = p.model;
        doc["gestures"] = json::array();
        for (const auto& gesture : p.gestures) {
            json points = json::array();
            for (const auto& point : gesture.points) points.push_back({{"beat", point.beat}, {"fret", point.fret}});
            doc["gestures"].push_back({{"id", gesture.id}, {"type", gesture.type}, {"beat", gesture.beat}, {"string", gesture.string_index + 1}, {"points", points}});
        }
    }
    return doc.dump(2) + "\n";
}

Pattern load_pattern(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    require(!error, "Cannot inspect pattern: " + path.u8string() + " (filesystem error " + std::to_string(error.value()) + ")");
    require(size <= max_document_bytes, "Pattern JSON exceeds the 1 MiB prototype limit");
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "Cannot open pattern: " + path.u8string());
    std::string source(static_cast<std::size_t>(size), '\0');
    in.read(source.data(), static_cast<std::streamsize>(source.size()));
    require(in.gcount() == static_cast<std::streamsize>(source.size()), "Could not read complete pattern");
    return parse_pattern_json(source);
}

void save_pattern(const Pattern& p, const std::filesystem::path& path) {
    const auto text = pattern_json(p);
    require(text.size() <= max_document_bytes, "Serialized pattern exceeds the 1 MiB prototype limit");
    write_file(path, [&](std::ostream& out) { out.write(text.data(), static_cast<std::streamsize>(text.size())); });
}

std::string render_identity(const Pattern& p, const RenderOptions& options) {
    require(options.sample_rate >= 8000 && options.sample_rate <= 192000, "sample_rate must be in 8000..192000");
    require(std::isfinite(options.tail_seconds) && options.tail_seconds >= 0 && options.tail_seconds <= 30, "tail must be in 0..30 seconds");
    // Versioned declarative bytes, never a compiler-dependent C struct dump.
    json identity{{"schema", "gtracker.render-identity"}, {"version", 1}, {"renderer", GT_VERSION},
        {"pattern", json::parse(pattern_json(p))}, {"sample_rate", options.sample_rate},
        {"instrument_tail_seconds", options.tail_seconds}, {"bypass_production", options.bypass_production}};
    if (options.production_override) identity["production_override"] = production_value(*options.production_override);
    return sha256(identity.dump());
}

RenderResult render_pattern(const Pattern& p, const RenderOptions& options) {
    validate_pattern(p);
    require(options.sample_rate >= 8000 && options.sample_rate <= 192000, "sample_rate must be in 8000..192000");
    require(std::isfinite(options.tail_seconds) && options.tail_seconds >= 0 && options.tail_seconds <= 30, "tail must be in 0..30 seconds");
    const double seconds_per_beat = 60.0 / p.bpm;
    auto production = options.production_override.value_or(p.production.value_or(ProductionSettings{}));
    validate_production(production);
    if (options.bypass_production) production.enabled = false;
    const auto channels = production.enabled ? 2 : 1;
    const double dry_frame_count = std::ceil((p.length_beats * seconds_per_beat + options.tail_seconds) * options.sample_rate);
    const double room_tail = production_tail_seconds(production);
    const double frame_count = dry_frame_count + std::ceil(room_tail * options.sample_rate);
    require(frame_count >= 1 && frame_count * channels <= max_render_samples, "Render exceeds the prototype's 32 Mi-sample buffer limit; render a shorter pattern or lower sample rate");
    const auto dry_frames = static_cast<std::size_t>(dry_frame_count);
    auto config = gt_default_config();
    config.sample_rate = static_cast<std::uint32_t>(options.sample_rate);
    config.string_count = static_cast<std::uint32_t>(p.tuning_midi.size());
    config.seed = p.seed;
    config.decay_seconds = p.decay_seconds;
    config.brightness = p.brightness;
    config.body_mix = p.body_mix;
    for (std::size_t i = 0; i < p.tuning_midi.size(); ++i)
        config.tuning_hz[i] = static_cast<float>(440.0 * std::pow(2.0, (p.tuning_midi[i] - 69.0) / 12.0));
    gt_synth* raw = nullptr;
    check_core(gt_create_model(&config, p.model == "contact_0_2" ? GT_MODEL_CONTACT_0_2 : GT_MODEL_BASELINE_0_1, &raw));
    std::unique_ptr<gt_synth, decltype(&gt_destroy)> synth(raw, gt_destroy);
    auto ordered = p.events;
    std::stable_sort(ordered.begin(), ordered.end(), [](const Event& a, const Event& b) { return a.beat < b.beat; });
    std::vector<gt_event> events;
    const auto musical_frames = static_cast<std::uint64_t>(std::ceil(p.length_beats * seconds_per_beat * options.sample_rate));
    for (const auto& source : ordered) {
        if (source.type == "pluck") {
            const double frequency = config.tuning_hz[source.string_index] * std::pow(2.0, source.fret / 12.0);
            require(frequency <= options.sample_rate / 4.0, "Pluck '" + source.id + "' exceeds this sample rate's pitch limit; use a higher sample rate or lower fret");
        }
        gt_event event{};
        event.frame = std::min(static_cast<std::uint64_t>(std::llround(source.beat * seconds_per_beat * options.sample_rate)), musical_frames - 1);
        event.type = source.type == "pluck" ? GT_EVENT_PLUCK : source.type == "damp" ? GT_EVENT_DAMP : GT_EVENT_BODY_HIT;
        event.string_index = source.type == "body_hit" ? 0 : static_cast<std::uint32_t>(source.string_index);
        event.fret = source.fret;
        event.strength = source.strength;
        events.push_back(event);
    }
    auto ordered_gestures = p.gestures;
    std::stable_sort(ordered_gestures.begin(), ordered_gestures.end(), [](const Gesture& a, const Gesture& b) { return a.beat < b.beat; });
    std::vector<std::vector<gt_fret_point>> point_storage;
    std::vector<gt_slide> slides;
    point_storage.reserve(ordered_gestures.size()); slides.reserve(ordered_gestures.size());
    for (const auto& gesture : ordered_gestures) {
        gt_slide slide{};
        slide.frame = static_cast<std::uint64_t>(std::llround(gesture.beat * seconds_per_beat * options.sample_rate));
        slide.string_index = static_cast<std::uint32_t>(gesture.string_index);
        require(slide.frame < musical_frames, "Slide '" + gesture.id + "' starts beyond the pattern after sample rounding");
        point_storage.emplace_back();
        auto& points = point_storage.back();
        for (const auto& source : gesture.points) {
            const double frequency = config.tuning_hz[slide.string_index] * std::exp2(static_cast<double>(source.fret) / 12.0);
            require(frequency <= options.sample_rate / 4.0, "Slide '" + gesture.id + "' exceeds this sample rate's pitch limit");
            const auto frame = static_cast<std::uint64_t>(std::llround((gesture.beat + source.beat) * seconds_per_beat * options.sample_rate));
            require(frame <= musical_frames, "Slide '" + gesture.id + "' ends beyond the pattern after sample rounding");
            require(frame >= slide.frame && (points.empty() || frame - slide.frame > points.back().frame_offset),
                    "Slide '" + gesture.id + "' has points closer than one sample frame; increase sample rate or separate the points");
            points.push_back({frame - slide.frame, source.fret});
        }
        slide.points = points.data(); slide.point_count = points.size();
        slides.push_back(slide);
    }
    const auto loaded = gt_set_performance(synth.get(), events.data(), events.size(), slides.data(), slides.size());
    require(loaded != GT_INVALID_ARGUMENT, "Performance curves/events conflict after sample rounding; increase sample rate or separate them");
    check_core(loaded);
    RenderResult result;
    result.sample_rate = options.sample_rate;
    result.channels = channels;
    result.production_tail_seconds = room_tail;
    result.identity = render_identity(p, options);
    result.samples.resize(static_cast<std::size_t>(frame_count) * static_cast<std::size_t>(channels));
    const auto start = std::chrono::steady_clock::now();
    if (channels == 1) {
        for (std::size_t offset = 0; offset < result.frames(); offset += 1024)
            check_core(gt_render(synth.get(), result.samples.data() + offset, std::min<std::size_t>(1024, result.frames() - offset)));
    } else {
        ProductionProcessor processor(options.sample_rate, production);
        std::array<float, 1024> dry{};
        for (std::size_t offset = 0; offset < result.frames(); offset += dry.size()) {
            const auto count = std::min(dry.size(), result.frames() - offset);
            std::fill(dry.begin(), dry.end(), 0);
            if (offset < dry_frames) check_core(gt_render(synth.get(), dry.data(), std::min(count, dry_frames - offset)));
            processor.process(dry.data(), result.samples.data() + offset * 2, count);
        }
    }
    result.elapsed_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    double squares = 0;
    const auto end_begin = result.samples.size() - std::min(result.samples.size(), static_cast<std::size_t>(options.sample_rate / 20 * channels));
    for (std::size_t i = 0; i < result.samples.size(); ++i) {
        const float value = result.samples[i];
        require(std::isfinite(value), "Synthesizer produced non-finite audio");
        result.peak = std::max(result.peak, std::abs(value));
        squares += static_cast<double>(value) * value;
        if (std::abs(value) >= 1) ++result.clipped_samples;
        if (i >= end_begin) result.end_peak = std::max(result.end_peak, std::abs(value));
    }
    result.rms = static_cast<float>(std::sqrt(squares / static_cast<double>(result.samples.size())));
    return result;
}

void write_wav(const std::filesystem::path& path, const std::vector<float>& samples, int sample_rate, int channels) {
    require(sample_rate >= 8000 && sample_rate <= 192000, "WAV sample rate is out of range");
    require(channels == 1 || channels == 2, "WAV requires one or two channels");
    require(samples.size() % static_cast<std::size_t>(channels) == 0, "WAV requires complete interleaved frames");
    require(samples.size() <= (std::numeric_limits<std::uint32_t>::max() - 36ULL) / 2, "WAV exceeds RIFF size limit");
    for (float value : samples) require(std::isfinite(value), "Cannot write non-finite WAV samples");
    write_file(path, [&](std::ostream& out) {
        const auto u16 = [&](std::uint16_t v) { out.put(static_cast<char>(v & 255)); out.put(static_cast<char>((v >> 8) & 255)); };
        const auto u32 = [&](std::uint32_t v) { u16(static_cast<std::uint16_t>(v)); u16(static_cast<std::uint16_t>(v >> 16)); };
        const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
        out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16);
        u16(1); u16(static_cast<std::uint16_t>(channels)); u32(static_cast<std::uint32_t>(sample_rate)); u32(static_cast<std::uint32_t>(sample_rate) * 2 * static_cast<std::uint32_t>(channels));
        u16(static_cast<std::uint16_t>(2 * channels)); u16(16); out.write("data", 4); u32(bytes);
        for (float value : samples) {
            const auto sample = static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
            u16(static_cast<std::uint16_t>(sample));
        }
    });
}

void write_pcm16_wav(const std::filesystem::path& path, const std::vector<std::int16_t>& samples, int sample_rate, int channels) {
    require(sample_rate >= 8000 && sample_rate <= 192000, "WAV sample rate is out of range");
    require(channels == 1 || channels == 2, "Captured WAV requires one or two channels");
    require(!samples.empty() && samples.size() % static_cast<std::size_t>(channels) == 0, "Captured WAV needs complete nonempty PCM frames");
    require(samples.size() <= (std::numeric_limits<std::uint32_t>::max() - 36ULL) / 2, "WAV exceeds RIFF size limit");
    write_file(path, [&](std::ostream& out) {
        const auto u16 = [&](std::uint16_t value) { out.put(static_cast<char>(value & 255)); out.put(static_cast<char>(value >> 8)); };
        const auto u32 = [&](std::uint32_t value) { u16(static_cast<std::uint16_t>(value)); u16(static_cast<std::uint16_t>(value >> 16)); };
        const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
        const auto frame_bytes = static_cast<std::uint16_t>(channels * 2);
        out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16);
        u16(1); u16(static_cast<std::uint16_t>(channels)); u32(static_cast<std::uint32_t>(sample_rate));
        u32(static_cast<std::uint32_t>(sample_rate) * frame_bytes); u16(frame_bytes); u16(16);
        out.write("data", 4); u32(bytes);
        for (auto value : samples) u16(static_cast<std::uint16_t>(value));
    });
}

void write_text_atomic(const std::filesystem::path& path, std::string_view text) {
    write_file(path, [&](std::ostream& out) { out.write(text.data(), static_cast<std::streamsize>(text.size())); });
}
} // namespace gtracker
