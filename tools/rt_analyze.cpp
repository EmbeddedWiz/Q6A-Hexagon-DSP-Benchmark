// RT raw-data analysis for the Q6A DSP benchmark.
// C++17 only: percentile tables, bootstrap CIs, deadline suitability,
// drift regression and dependency-free SVG plots.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kClockHz = 1.170e9; // direct DSP cycle-clock probe (P1.2)
constexpr int kBootstrap = 1000;
constexpr unsigned kSeed = 0x5EED1234u;

struct Sample {
    double ns = 0.0;
    double cycles = 0.0;
    double temperature = 0.0;
    std::uint64_t time_ns = 0;
    int round = 0;
    std::uint64_t index = 0;
};

struct Group {
    int condition = 0;
    std::string config;
    std::vector<Sample> samples;
};

struct CI {
    double lo = 0.0;
    double hi = 0.0;
};

struct Stats {
    std::size_t n = 0;
    double min = 0.0, p25 = 0.0, p50 = 0.0, p75 = 0.0;
    double p90 = 0.0, p99 = 0.0, p999 = 0.0, p9999 = 0.0, max = 0.0;
    double iqr = 0.0, jitter = 0.0;
    CI median_ci, p99_ci;
    double temp_min = 0.0, temp_max = 0.0;
    std::vector<double> median_boot, p99_boot;
};

std::string condition_name(int c)
{
    switch (c) {
    case 1: return "idle";
    case 2: return "cpu-load";
    case 3: return "memory-load";
    case 4: return "dsp-load";
    default: return "condition-" + std::to_string(c);
    }
}

std::string variant_name(const std::string& c)
{
    if (c == "cpu") return "CPU A78 scalar";
    if (c == "dsp") return "DSP compute only";
    if (c == "dsp-rpc-step") return "DSP + FastRPC step";
    if (c == "dsp-rpc-bench") return "DSP + FastRPC bench (16 steps/call)";
    return c;
}

std::string sanitize(const std::string& s)
{
    std::string out;
    for (char ch : s) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9')) {
            out.push_back(ch);
        } else {
            out.push_back('-');
        }
    }
    return out;
}

double quantile_sorted(const std::vector<double>& v, double p)
{
    if (v.empty()) return 0.0;
    if (v.size() == 1) return v[0];
    const double pos = (v.size() - 1) * p;
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    const double frac = pos - static_cast<double>(lo);
    return v[lo] * (1.0 - frac) + v[hi] * frac;
}

double quantile_inplace(std::vector<double>& v, double p)
{
    if (v.empty()) return 0.0;
    if (v.size() == 1) return v[0];
    const double pos = (v.size() - 1) * p;
    const std::size_t lo = static_cast<std::size_t>(std::floor(pos));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(pos));
    std::nth_element(v.begin(), v.begin() + lo, v.end());
    const double a = v[lo];
    if (hi == lo) return a;
    std::nth_element(v.begin() + lo + 1, v.begin() + hi, v.end());
    const double b = v[hi];
    const double frac = pos - static_cast<double>(lo);
    return a * (1.0 - frac) + b * frac;
}

Stats compute_stats(const Group& group)
{
    Stats s;
    s.n = group.samples.size();
    if (s.n == 0) return s;
    std::vector<double> v;
    v.reserve(s.n);
    double tmin = 1e9, tmax = -1e9;
    for (const auto& x : group.samples) {
        v.push_back(x.ns);
        tmin = std::min(tmin, x.temperature);
        tmax = std::max(tmax, x.temperature);
    }
    s.temp_min = tmin;
    s.temp_max = tmax;
    std::sort(v.begin(), v.end());
    s.min = v.front();
    s.p25 = quantile_sorted(v, 0.25);
    s.p50 = quantile_sorted(v, 0.5);
    s.p75 = quantile_sorted(v, 0.75);
    s.p90 = quantile_sorted(v, 0.90);
    s.p99 = quantile_sorted(v, 0.99);
    s.p999 = quantile_sorted(v, 0.999);
    s.p9999 = quantile_sorted(v, 0.9999);
    s.max = v.back();
    s.iqr = s.p75 - s.p25;
    s.jitter = s.p99 - s.p50;

    // Bootstrap median and p99. The same resampled vector is reused.
    std::mt19937_64 rng(kSeed + static_cast<unsigned>(s.n) +
                        static_cast<unsigned>(group.condition * 131) +
                        static_cast<unsigned>(group.config.size() * 17));
    std::uniform_int_distribution<std::size_t> dist(0, s.n - 1);
    std::vector<double> buf(s.n);
    s.median_boot.reserve(kBootstrap);
    s.p99_boot.reserve(kBootstrap);
    for (int b = 0; b < kBootstrap; ++b) {
        for (std::size_t i = 0; i < s.n; ++i) buf[i] = v[dist(rng)];
        s.median_boot.push_back(quantile_inplace(buf, 0.5));
        s.p99_boot.push_back(quantile_inplace(buf, 0.99));
    }
    std::sort(s.median_boot.begin(), s.median_boot.end());
    std::sort(s.p99_boot.begin(), s.p99_boot.end());
    s.median_ci = {quantile_sorted(s.median_boot, 0.025),
                   quantile_sorted(s.median_boot, 0.975)};
    s.p99_ci = {quantile_sorted(s.p99_boot, 0.025),
                quantile_sorted(s.p99_boot, 0.975)};
    return s;
}

struct Row {
    int condition;
    int round;
    std::string config;
    std::uint64_t index;
    double value;
    std::string unit;
    int steps;
    double temperature;
    std::uint64_t time_ns;
    int warmup;
};

bool parse_row(const std::string& line, Row& row)
{
    std::stringstream ss(line);
    std::string field;
    std::vector<std::string> f;
    while (std::getline(ss, field, ',')) f.push_back(field);
    if (f.size() < 10) return false;
    try {
        row.condition = std::stoi(f[0]);
        row.round = std::stoi(f[1]);
        row.config = f[2];
        row.index = std::stoull(f[3]);
        row.value = std::stod(f[4]);
        row.unit = f[5];
        row.steps = std::max(1, std::stoi(f[6]));
        row.temperature = std::stod(f[7]);
        row.time_ns = std::stoull(f[8]);
        row.warmup = std::stoi(f[9]);
    } catch (...) {
        return false;
    }
    return true;
}

std::string group_key(int condition, const std::string& config)
{
    return std::to_string(condition) + "|" + config;
}

void print_table_row(std::ostream& out, const Group& g, const Stats& s)
{
    out << "| " << condition_name(g.condition) << " | "
        << variant_name(g.config) << " | " << s.n << " | "
        << std::fixed << std::setprecision(1)
        << s.min << " | " << s.p50 << " [" << s.median_ci.lo << ", "
        << s.median_ci.hi << "] | " << s.p90 << " | " << s.p99 << " ["
        << s.p99_ci.lo << ", " << s.p99_ci.hi << "] | " << s.p999 << " | ";
    if (s.n >= 100000) {
        out << s.p9999;
    } else {
        out << "n/a";
    }
    out << " | " << s.max << " | " << s.iqr << " | " << s.jitter
        << " | " << s.temp_min << "-" << s.temp_max << " |\n";
}

void write_histogram(const fs::path& path, const Group& g, const Stats& s)
{
    constexpr int width = 900;
    constexpr int height = 450;
    constexpr int left = 80;
    constexpr int right = 30;
    constexpr int top = 40;
    constexpr int bottom = 60;
    constexpr int bins = 80;
    const int plot_w = width - left - right;
    const int plot_h = height - top - bottom;

    double lo = std::max(1e-3, s.min);
    double hi = std::max(lo * 1.000001, s.max);
    if (hi <= lo) hi = lo * 1.0001;
    std::vector<double> edges(bins + 1);
    const double l0 = std::log10(lo);
    const double l1 = std::log10(hi);
    for (int i = 0; i <= bins; ++i) {
        edges[i] = std::pow(10.0, l0 + (l1 - l0) * i / bins);
    }
    std::vector<std::uint64_t> counts(bins, 0);
    for (const auto& x : g.samples) {
        if (x.ns < lo || x.ns > hi) continue;
        const double pos = (std::log10(x.ns) - l0) / (l1 - l0) * bins;
        int b = static_cast<int>(std::floor(pos));
        b = std::clamp(b, 0, bins - 1);
        ++counts[b];
    }
    std::uint64_t max_count = 1;
    for (auto c : counts) max_count = std::max(max_count, c);
    const double ymax = std::log10(1.0 + static_cast<double>(max_count));

    std::ofstream svg(path);
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    svg << "<text x=\"" << width / 2 << "\" y=\"24\" text-anchor=\"middle\" "
        << "font-family=\"sans-serif\" font-size=\"16\">"
        << condition_name(g.condition) << " / " << variant_name(g.config)
        << " (n=" << s.n << ", log10 count)</text>\n";
    svg << "<line x1=\"" << left << "\" y1=\"" << top + plot_h << "\" x2=\""
        << left + plot_w << "\" y2=\"" << top + plot_h
        << "\" stroke=\"black\"/>\n";
    svg << "<line x1=\"" << left << "\" y1=\"" << top << "\" x2=\"" << left
        << "\" y2=\"" << top + plot_h << "\" stroke=\"black\"/>\n";
    const double bar_w = static_cast<double>(plot_w) / bins;
    for (int i = 0; i < bins; ++i) {
        if (counts[i] == 0) continue;
        const double y = std::log10(1.0 + static_cast<double>(counts[i]));
        const int h = static_cast<int>(y / ymax * plot_h);
        const int x = left + static_cast<int>(i * bar_w);
        svg << "<rect x=\"" << x << "\" y=\"" << top + plot_h - h
            << "\" width=\"" << std::max(1.0, bar_w - 0.5) << "\" height=\""
            << h << "\" fill=\"#2f6f9f\"/>\n";
    }
    for (int e = 0; e <= 4; ++e) {
        const double y = ymax * e / 4.0;
        const int py = top + plot_h - static_cast<int>(y / ymax * plot_h);
        svg << "<line x1=\"" << left - 5 << "\" y1=\"" << py << "\" x2=\""
            << left << "\" y2=\"" << py << "\" stroke=\"black\"/>\n";
        svg << "<text x=\"" << left - 10 << "\" y=\"" << py + 4
            << "\" text-anchor=\"end\" font-family=\"sans-serif\" "
            << "font-size=\"11\">" << std::setprecision(0) << y << "</text>\n";
    }
    for (int e = 0; e <= 4; ++e) {
        const double lx = l0 + (l1 - l0) * e / 4.0;
        const int px = left + plot_w * e / 4;
        svg << "<line x1=\"" << px << "\" y1=\"" << top + plot_h
            << "\" x2=\"" << px << "\" y2=\"" << top + plot_h + 5
            << "\" stroke=\"black\"/>\n";
        svg << "<text x=\"" << px << "\" y=\"" << top + plot_h + 20
            << "\" text-anchor=\"middle\" font-family=\"sans-serif\" "
            << "font-size=\"11\">" << std::setprecision(3)
            << std::pow(10.0, lx) << "</text>\n";
    }
    svg << "<text x=\"" << left + plot_w / 2 << "\" y=\"" << height - 15
        << "\" text-anchor=\"middle\" font-family=\"sans-serif\" "
        << "font-size=\"12\">ns per step (log x)</text>\n";
    svg << "</svg>\n";
}

void write_timeseries(const fs::path& path, const Group& g, const Stats& s,
                      double slope)
{
    constexpr int width = 900;
    constexpr int height = 350;
    constexpr int left = 80;
    constexpr int top = 40;
    constexpr int plot_w = width - left - 30;
    constexpr int plot_h = height - top - 60;
    std::vector<const Sample*> ordered;
    ordered.reserve(g.samples.size());
    for (const auto& x : g.samples) ordered.push_back(&x);
    std::sort(ordered.begin(), ordered.end(), [](const Sample* a, const Sample* b) {
        if (a->time_ns != b->time_ns) return a->time_ns < b->time_ns;
        if (a->round != b->round) return a->round < b->round;
        return a->index < b->index;
    });
    const std::size_t stride = std::max<std::size_t>(1, ordered.size() / 2000);
    std::vector<double> xs, ys;
    for (std::size_t i = 0; i < ordered.size(); i += stride) {
        xs.push_back(static_cast<double>(i));
        ys.push_back(ordered[i]->ns);
    }
    double ymin = s.p50, ymax = s.p50;
    for (double y : ys) { ymin = std::min(ymin, y); ymax = std::max(ymax, y); }
    if (ymax <= ymin) ymax = ymin + 1.0;
    std::ofstream svg(path);
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\">\n";
    svg << "<rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n";
    svg << "<text x=\"" << width / 2 << "\" y=\"24\" text-anchor=\"middle\" "
        << "font-family=\"sans-serif\" font-size=\"16\">"
        << condition_name(g.condition) << " / " << variant_name(g.config)
        << " time series (slope=" << std::setprecision(4) << slope
        << " ns/sample)</text>\n";
    svg << "<line x1=\"" << left << "\" y1=\"" << top + plot_h << "\" x2=\""
        << left + plot_w << "\" y2=\"" << top + plot_h
        << "\" stroke=\"black\"/>\n";
    svg << "<line x1=\"" << left << "\" y1=\"" << top << "\" x2=\"" << left
        << "\" y2=\"" << top + plot_h << "\" stroke=\"black\"/>\n";
    svg << "<polyline fill=\"none\" stroke=\"#2f6f9f\" stroke-width=\"1\" points=\"";
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const int px = left + static_cast<int>(xs[i] / std::max(1.0, xs.back()) * plot_w);
        const int py = top + plot_h - static_cast<int>((ys[i] - ymin) / (ymax - ymin) * plot_h);
        svg << px << "," << py << " ";
    }
    svg << "\"/>\n";
    svg << "<text x=\"" << left - 10 << "\" y=\"" << top + 10
        << "\" text-anchor=\"end\" font-family=\"sans-serif\" font-size=\"11\">"
        << std::setprecision(0) << ymax << "</text>\n";
    svg << "<text x=\"" << left - 10 << "\" y=\"" << top + plot_h
        << "\" text-anchor=\"end\" font-family=\"sans-serif\" font-size=\"11\">"
        << ymin << "</text>\n";
    svg << "<text x=\"" << left + plot_w / 2 << "\" y=\"" << height - 15
        << "\" text-anchor=\"middle\" font-family=\"sans-serif\" font-size=\"12\">"
        << "sample ordinal (downsampled)</text>\n";
    svg << "</svg>\n";
}

double drift_slope(const Group& g)
{
    std::vector<const Sample*> ordered;
    for (const auto& x : g.samples) ordered.push_back(&x);
    std::sort(ordered.begin(), ordered.end(), [](const Sample* a, const Sample* b) {
        if (a->time_ns != b->time_ns) return a->time_ns < b->time_ns;
        if (a->round != b->round) return a->round < b->round;
        return a->index < b->index;
    });
    const double n = static_cast<double>(ordered.size());
    if (n < 2) return 0.0;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const double x = static_cast<double>(i);
        const double y = ordered[i]->ns;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const double den = n * sxx - sx * sx;
    return (den == 0.0) ? 0.0 : (n * sxy - sx * sy) / den;
}

CI ci_from_sample(const std::vector<double>& boot)
{
    return {quantile_sorted(boot, 0.025), quantile_sorted(boot, 0.975)};
}

void write_effect(std::ostream& out, const std::string& label,
                  const Stats& a, const Stats& b)
{
    // a / b ratio and a - b difference, using the independent bootstrap draws.
    std::vector<double> ratios, med_diffs, p99_diffs;
    const std::size_t n = std::min({a.median_boot.size(), b.median_boot.size(),
                                    a.p99_boot.size(), b.p99_boot.size()});
    for (std::size_t i = 0; i < n; ++i) {
        if (b.median_boot[i] > 0.0) ratios.push_back(a.median_boot[i] / b.median_boot[i]);
        med_diffs.push_back(a.median_boot[i] - b.median_boot[i]);
        p99_diffs.push_back(a.p99_boot[i] - b.p99_boot[i]);
    }
    std::sort(ratios.begin(), ratios.end());
    std::sort(med_diffs.begin(), med_diffs.end());
    std::sort(p99_diffs.begin(), p99_diffs.end());
    const CI r = ci_from_sample(ratios);
    const CI md = ci_from_sample(med_diffs);
    const CI pd = ci_from_sample(p99_diffs);
    const double ratio = (b.p50 > 0.0) ? a.p50 / b.p50 : 0.0;
    out << "| " << label << " | " << std::fixed << std::setprecision(3)
        << ratio << " [" << r.lo << ", " << r.hi << "] | "
        << std::setprecision(1) << (a.p50 - b.p50) << " [" << md.lo << ", "
        << md.hi << "] | " << (a.p99 - b.p99) << " [" << pd.lo << ", "
        << pd.hi << "] |\n";
}

} // namespace

int main(int argc, char** argv)
{
    fs::path input = "results/rt-2026-11-14";
    fs::path output;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input" && i + 1 < argc) input = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output = argv[++i];
        else {
            std::cerr << "usage: rt_analyze --input DIR [--output DIR]\n";
            return 2;
        }
    }
    if (output.empty()) output = input;
    fs::create_directories(output / "figures");

    auto json_number = [](const std::string& text, const std::string& key) {
        const std::string needle = "\"" + key + "\":";
        const auto p = text.find(needle);
        if (p == std::string::npos) return 0.0;
        return std::stod(text.substr(p + needle.size()));
    };
    double cpu_timer_ns = 0.0;
    double dsp_timer_cycles = 0.0;
    {
        std::ifstream f(input / "timer-overhead-cpu.json");
        if (f) {
            std::string text((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
            cpu_timer_ns = json_number(text, "ns_per_call");
        }
        std::ifstream g(input / "timer-overhead-dsp.jsonl");
        if (g) {
            std::string line;
            double sum = 0.0;
            int count = 0;
            while (std::getline(g, line)) {
                const double v = json_number(line, "cycles_per_call");
                if (v > 0.0) { sum += v; ++count; }
            }
            if (count > 0) dsp_timer_cycles = sum / count;
        }
    }

    std::map<std::string, Group> groups;
    std::uint64_t rows = 0;
    for (const auto& entry : fs::directory_iterator(input / "raw")) {
        if (!entry.is_regular_file()) continue;
        const std::string name = entry.path().filename().string();
        if (name.size() < 4 || name.substr(name.size() - 4) != ".csv") continue;
        std::ifstream in(entry.path());
        std::string line;
        std::getline(in, line); // header
        while (std::getline(in, line)) {
            Row row;
            if (!parse_row(line, row)) continue;
            if (row.warmup != 0) continue;
            const double per_step = row.value / static_cast<double>(row.steps);
            Sample s;
            if (row.unit == "cycles") {
                // Two timer reads per batch; the cost is removed explicitly.
                const double correction =
                    (dsp_timer_cycles > 0.0)
                        ? 2.0 * dsp_timer_cycles / static_cast<double>(row.steps)
                        : 0.0;
                s.cycles = std::max(0.0, per_step - correction);
                s.ns = s.cycles / (kClockHz / 1e9);
            } else {
                // Two host timer reads per sample/batch; removed explicitly.
                const double correction =
                    (cpu_timer_ns > 0.0) ? 2.0 * cpu_timer_ns : 0.0;
                s.ns = std::max(0.0, (row.value - correction) /
                                        static_cast<double>(row.steps));
                s.cycles = 0.0;
            }
            s.temperature = row.temperature;
            s.time_ns = row.time_ns;
            s.round = row.round;
            s.index = row.index;
            const std::string key = group_key(row.condition, row.config);
            auto& g = groups[key];
            g.condition = row.condition;
            g.config = row.config;
            g.samples.push_back(s);
            ++rows;
        }
    }
    if (groups.empty()) {
        std::cerr << "no raw CSV rows found\n";
        return 1;
    }

    std::map<std::string, Stats> stats;
    for (const auto& [key, g] : groups) {
        stats[key] = compute_stats(g);
    }

    // Ordering helper for deterministic tables.
    auto ordered_keys = [&]() {
        std::vector<std::string> keys;
        for (const auto& kv : groups) keys.push_back(kv.first);
        std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b) {
            auto split = [](const std::string& s) {
                const auto p = s.find('|');
                return std::pair<int, std::string>{std::stoi(s.substr(0, p)),
                                                   s.substr(p + 1)};
            };
            return split(a) < split(b);
        });
        return keys;
    }();
    auto rank = [](const std::string& config) {
        if (config == "cpu") return 0;
        if (config == "dsp") return 1;
        if (config == "dsp-rpc-step") return 2;
        if (config == "dsp-rpc-bench") return 3;
        return 4;
    };
    std::sort(ordered_keys.begin(), ordered_keys.end(), [&](const std::string& a, const std::string& b) {
        const auto pa = a.find('|');
        const auto pb = b.find('|');
        const int ca = std::stoi(a.substr(0, pa));
        const int cb = std::stoi(b.substr(0, pb));
        if (ca != cb) return ca < cb;
        return rank(a.substr(pa + 1)) < rank(b.substr(pb + 1));
    });

    std::ofstream md(output / "summary.md");
    md << "# RT measurement summary\n\n";
    md << "Generated by `tools/rt_analyze.cpp` from raw CSV rows: " << rows << ".\n";
    md << "Values are per step. Timer-read overhead is subtracted explicitly: "
          "two host reads per batch for CPU/RPC samples and two DSP cycle-counter "
          "reads per batch for DSP samples. DSP cycles are converted with the "
          "direct 1.170 GHz probe (`cycles / 1.170`). Jitter is `p99 - p50`.\n";
    md << "`p99.99` is shown only for n >= 100000; otherwise `n/a`.\n\n";

    md << "## L2: distributions (ns per step)\n\n";
    md << "| condition | variant | n | min | p50 [95% CI] | p90 | p99 [95% CI] | "
          "p99.9 | p99.99 | observed max | IQR | jitter | temp C |\n";
    md << "|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& key : ordered_keys) {
        print_table_row(md, groups.at(key), stats.at(key));
    }

    md << "\n## DSP cycle distribution (primary metric)\n\n";
    md << "| condition | n | min | p50 | p99 | p99.9 | observed max |\n";
    md << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& key : ordered_keys) {
        const auto& g = groups.at(key);
        if (g.config != "dsp") continue;
        std::vector<double> c;
        c.reserve(g.samples.size());
        for (const auto& x : g.samples) c.push_back(x.cycles);
        std::sort(c.begin(), c.end());
        md << "| " << condition_name(g.condition) << " | " << c.size() << " | "
           << std::fixed << std::setprecision(2) << c.front() << " | "
           << quantile_sorted(c, 0.5) << " | " << quantile_sorted(c, 0.99)
           << " | " << quantile_sorted(c, 0.999) << " | " << c.back() << " |\n";
    }

    md << "\n## L3: real-time suitability\n\n";
    md << "Verdict rule (fixed before the run): `годен` if p99.9 < period and "
          "observed max < period; `условно` if p99.9 < period but max >= period; "
          "`не годен` if p99.9 >= period. Observed max is not WCET.\n\n";
    md << "| condition | variant | period us | >period % | p99.9 margin us | "
          "max margin us | misses / 1e6 | verdict |\n";
    md << "|---|---|---:|---:|---:|---:|---:|---|\n";
    const double periods[] = {100.0, 50.0, 20.0};
    for (const auto& key : ordered_keys) {
        const auto& g = groups.at(key);
        const auto& s = stats.at(key);
        if (g.config != "cpu" && g.config != "dsp" && g.config != "dsp-rpc-step") continue;
        for (double period_us : periods) {
            const double period_ns = period_us * 1000.0;
            std::uint64_t over = 0;
            for (const auto& x : g.samples) if (x.ns > period_ns) ++over;
            const double fraction = static_cast<double>(over) / s.n;
            std::string verdict;
            if (s.p999 >= period_ns) verdict = "не годен";
            else if (s.max >= period_ns) verdict = "условно";
            else verdict = "годен";
            md << "| " << condition_name(g.condition) << " | "
               << variant_name(g.config) << " | " << std::fixed << std::setprecision(0)
               << period_us << " | " << std::setprecision(4) << fraction * 100.0
               << " | " << std::setprecision(2) << (period_ns - s.p999) / 1000.0
               << " | " << (period_ns - s.max) / 1000.0 << " | "
               << std::setprecision(1) << fraction * 1e6 << " | " << verdict << " |\n";
        }
    }

    md << "\n## L4: condition comparison (tail only)\n\n";
    md << "| variant | condition | p50 ns | p99 ns | p99.9 ns | observed max ns |\n";
    md << "|---|---|---:|---:|---:|---:|\n";
    for (const auto& key : ordered_keys) {
        const auto& g = groups.at(key);
        const auto& s = stats.at(key);
        if (g.config == "dsp-rpc-bench") continue;
        md << "| " << variant_name(g.config) << " | " << condition_name(g.condition)
           << " | " << std::fixed << std::setprecision(1) << s.p50 << " | "
           << s.p99 << " | " << s.p999 << " | " << s.max << " |\n";
    }

    md << "\n## L8: effect sizes (DSP and CPU)\n\n";
    md << "Ratio and differences use independent bootstrap draws (1000 repeats, "
          "95% interval). Positive difference means the first variant is slower.\n\n";
    md << "| comparison (A / B) | median ratio [CI] | median diff ns [CI] | "
          "p99 diff ns [CI] |\n";
    md << "|---|---:|---:|---:|\n";
    for (int condition = 1; condition <= 3; ++condition) {
        const std::string cpu = group_key(condition, "cpu");
        const std::string dsp = group_key(condition, "dsp");
        const std::string step = group_key(condition, "dsp-rpc-step");
        if (!groups.count(cpu) || !groups.count(dsp) || !groups.count(step)) continue;
        const std::string cname = condition_name(condition);
        write_effect(md, cname + ": DSP compute / CPU", stats.at(dsp), stats.at(cpu));
        write_effect(md, cname + ": DSP+RPC step / CPU", stats.at(step), stats.at(cpu));
        write_effect(md, cname + ": DSP+RPC step / DSP compute", stats.at(step), stats.at(dsp));
    }

    md << "\n## L7: drift and time series\n\n";
    md << "Slope is an ordinary least-squares fit against sample ordinal after "
          "sorting by acquisition/retrieval time. A slope near zero means no "
          "material monotonic drift over the measured window.\n\n";
    md << "| condition | variant | slope ns/sample | relative change over series | figure |\n";
    md << "|---|---|---:|---:|---|\n";
    for (const auto& key : ordered_keys) {
        const auto& g = groups.at(key);
        const auto& s = stats.at(key);
        if ((g.config != "cpu" && g.config != "dsp" && g.config != "dsp-rpc-step") ||
            g.condition != 1) continue;
        const double slope = drift_slope(g);
        const double relative = (s.p50 > 0.0)
            ? slope * static_cast<double>(s.n) / s.p50 * 100.0 : 0.0;
        const std::string file = "figures/ts-" + sanitize(g.config) + "-cond"
            + std::to_string(g.condition) + ".svg";
        write_timeseries(output / file, g, s, slope);
        md << "| " << condition_name(g.condition) << " | " << variant_name(g.config)
           << " | " << std::fixed << std::setprecision(5) << slope << " | "
           << std::setprecision(2) << relative << "% | [" << file << "](" << file
           << ") |\n";
    }

    md << "\n## Histograms\n\n";
    for (const auto& key : ordered_keys) {
        const auto& g = groups.at(key);
        const auto& s = stats.at(key);
        const std::string file = "figures/hist-" + sanitize(g.config) + "-cond"
            + std::to_string(g.condition) + ".svg";
        write_histogram(output / file, g, s);
        md << "- [" << file << "](" << file << "): "
           << condition_name(g.condition) << " / " << variant_name(g.config) << "\n";
    }

    md << "\n## L6: timer-read overhead\n\n";
    md << "| source | calls | cost |\n";
    md << "|---|---:|---:|\n";
    std::ifstream cpu_timer(input / "timer-overhead-cpu.json");
    if (cpu_timer) {
        std::string text((std::istreambuf_iterator<char>(cpu_timer)),
                         std::istreambuf_iterator<char>());
        const double ns = json_number(text, "ns_per_call");
        if (ns > 0.0) {
            md << "| CPU `clock_gettime(CLOCK_MONOTONIC_RAW)` | 1000000 | "
               << std::fixed << std::setprecision(3) << ns << " ns/call |\n";
        }
    }
    std::ifstream dsp_timer(input / "timer-overhead-dsp.jsonl");
    if (dsp_timer) {
        std::string line;
        double sum = 0.0;
        int count = 0;
        while (std::getline(dsp_timer, line)) {
            const double cycles = json_number(line, "cycles_per_call");
            if (cycles > 0.0) {
                sum += cycles;
                ++count;
            }
        }
        if (count > 0) {
            md << "| DSP `qurt_get_core_pcycles` | " << count * 100000
               << " | " << std::fixed << std::setprecision(3)
               << sum / count << " cycles/call (~"
               << (sum / count) / 1.170 << " ns/call) |\n";
        }
    }
    md << "\nRaw files: `raw/*.csv`; environment: `environment.txt`; "
          "bench data are in 10.5k-call chunks.\n";
    md.close();

    std::cout << "wrote " << (output / "summary.md") << " with "
              << groups.size() << " groups, " << rows << " rows\n";
    return 0;
}
