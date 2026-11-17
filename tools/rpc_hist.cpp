// Dependency-free analysis of rpc-scaling raw CSV rows.
// Usage: rpc_hist <csv> <steps> <output-prefix> [label]
// Reads "steps,repeat,wall_us" rows, filters one step count, and writes
// percentiles, two-means cluster counts, histogram CSV and an SVG.
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

namespace {

double percentile(const std::vector<double>& sorted, double fraction)
{
    std::size_t index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(sorted.size())));
    if (index == 0U) {
        index = 1U;
    }
    if (index > sorted.size()) {
        index = sorted.size();
    }
    return sorted[index - 1U];
}

std::vector<double> read_samples(const std::string& path, int steps)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("cannot open " + path);
    }
    std::vector<double> values;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == 's') {
            continue;
        }
        std::istringstream row(line);
        int row_steps = 0;
        int repeat = 0;
        double wall_us = 0.0;
        char comma1 = 0;
        char comma2 = 0;
        if (!(row >> row_steps >> comma1 >> repeat >> comma2 >> wall_us)) {
            continue;
        }
        if (comma1 != ',' || comma2 != ',' || row_steps != steps) {
            continue;
        }
        values.push_back(wall_us);
    }
    if (values.empty()) {
        throw std::runtime_error("no samples for steps=" +
                                 std::to_string(steps));
    }
    std::sort(values.begin(), values.end());
    return values;
}

void write_histogram(const std::vector<double>& sorted,
                     const std::string& prefix, const std::string& label,
                     double high, std::size_t above_high)
{
    constexpr int kBins = 48;
    const double low = sorted.front();
    const double width = (high > low) ? (high - low) /
        static_cast<double>(kBins) : 1.0;
    std::vector<unsigned> counts(static_cast<std::size_t>(kBins), 0U);
    for (const double value : sorted) {
        if (value > high) {
            continue;
        }
        int bin = static_cast<int>((value - low) / width);
        if (bin < 0) {
            bin = 0;
        }
        if (bin >= kBins) {
            bin = kBins - 1;
        }
        ++counts[static_cast<std::size_t>(bin)];
    }
    unsigned peak = 1U;
    for (const unsigned count : counts) {
        peak = std::max(peak, count);
    }

    std::ofstream csv(prefix + ".hist.csv");
    csv << "bin_low_us,bin_high_us,count\n";
    for (int bin = 0; bin < kBins; ++bin) {
        csv << (low + static_cast<double>(bin) * width) << ','
            << (low + static_cast<double>(bin + 1) * width) << ','
            << counts[static_cast<std::size_t>(bin)] << '\n';
    }

    std::ofstream svg(prefix + ".svg");
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"720\" "
           "height=\"360\" viewBox=\"0 0 720 360\">\n";
    svg << "<rect width=\"720\" height=\"360\" fill=\"white\"/>\n";
    svg << "<text x=\"360\" y=\"24\" text-anchor=\"middle\" "
           "font-family=\"sans-serif\" font-size=\"14\">"
        << label << " (above p99: " << above_high << ")</text>\n";
    svg << "<line x1=\"60\" y1=\"320\" x2=\"700\" y2=\"320\" "
           "stroke=\"#333\"/>\n";
    svg << "<line x1=\"60\" y1=\"40\" x2=\"60\" y2=\"320\" "
           "stroke=\"#333\"/>\n";
    const double plot_width = 640.0;
    const double plot_height = 280.0;
    const double bar_width = plot_width / static_cast<double>(kBins);
    for (int bin = 0; bin < kBins; ++bin) {
        const double bar_height = plot_height *
            static_cast<double>(counts[static_cast<std::size_t>(bin)]) /
            static_cast<double>(peak);
        const double x = 60.0 + static_cast<double>(bin) * bar_width;
        const double y = 320.0 - bar_height;
        svg << "<rect x=\"" << x << "\" y=\"" << y
            << "\" width=\"" << (bar_width > 1.0 ? bar_width - 1.0 : bar_width)
            << "\" height=\"" << bar_height
            << "\" fill=\"#2f6db3\"/>\n";
    }
    svg << "<text x=\"60\" y=\"340\" font-family=\"sans-serif\" "
           "font-size=\"11\">" << low << " us</text>\n";
    svg << "<text x=\"700\" y=\"340\" text-anchor=\"end\" "
           "font-family=\"sans-serif\" font-size=\"11\">" << high
        << " us</text>\n";
    svg << "<text x=\"12\" y=\"320\" font-family=\"sans-serif\" "
           "font-size=\"11\">0</text>\n";
    svg << "<text x=\"12\" y=\"48\" font-family=\"sans-serif\" "
           "font-size=\"11\">" << peak << "</text>\n";
    svg << "</svg>\n";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "usage: rpc_hist <csv> <steps> <output-prefix> [label]\n";
        return 2;
    }
    const std::string csv = argv[1];
    const int steps = std::atoi(argv[2]);
    const std::string prefix = argv[3];
    const std::string label = (argc >= 5) ? argv[4] : ("N=" + std::to_string(steps));
    try {
        const std::vector<double> sorted = read_samples(csv, steps);
        double sum = 0.0;
        for (const double value : sorted) {
            sum += value;
        }
        const double mean = sum / static_cast<double>(sorted.size());
        std::cout << std::fixed << std::setprecision(3)
                  << "n=" << sorted.size()
                  << " min_us=" << sorted.front()
                  << " p50_us=" << percentile(sorted, 0.50)
                  << " p90_us=" << percentile(sorted, 0.90)
                  << " p99_us=" << percentile(sorted, 0.99)
                  << " max_us=" << sorted.back()
                  << " mean_us=" << mean << '\n';

        // A two-means split is reported only as a description. It does not
        // assume that the underlying process is bimodal.
        double c0 = sorted.front();
        double c1 = sorted.back();
        std::size_t n0 = 0U;
        std::size_t n1 = 0U;
        for (int iteration = 0; iteration < 100; ++iteration) {
            double s0 = 0.0;
            double s1 = 0.0;
            n0 = 0U;
            n1 = 0U;
            for (const double value : sorted) {
                if (std::abs(value - c0) <= std::abs(value - c1)) {
                    s0 += value;
                    ++n0;
                } else {
                    s1 += value;
                    ++n1;
                }
            }
            if (n0 == 0U || n1 == 0U) {
                break;
            }
            const double next0 = s0 / static_cast<double>(n0);
            const double next1 = s1 / static_cast<double>(n1);
            if (std::abs(next0 - c0) < 1e-6 &&
                std::abs(next1 - c1) < 1e-6) {
                c0 = next0;
                c1 = next1;
                break;
            }
            c0 = next0;
            c1 = next1;
        }
        if (n0 > 0U && n1 > 0U) {
            if (c0 > c1) {
                std::swap(c0, c1);
                std::swap(n0, n1);
            }
            std::cout << "two_means_low_us=" << c0
                      << " low_n=" << n0
                      << " low_fraction="
                      << (static_cast<double>(n0) /
                          static_cast<double>(sorted.size()))
                      << " two_means_high_us=" << c1
                      << " high_n=" << n1
                      << " high_fraction="
                      << (static_cast<double>(n1) /
                          static_cast<double>(sorted.size()))
                      << " split_us=" << (0.5 * (c0 + c1)) << '\n';
        }
        double histogram_high = percentile(sorted, 0.99);
        if (histogram_high <= sorted.front()) {
            histogram_high = sorted.back();
        }
        const auto above = std::upper_bound(
            sorted.begin(), sorted.end(), histogram_high);
        const std::size_t above_high = static_cast<std::size_t>(
            std::distance(above, sorted.end()));
        std::cout << "histogram_high_us=" << histogram_high
                  << " above_p99=" << above_high << '\n';
        write_histogram(sorted, prefix, label, histogram_high, above_high);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "rpc_hist: " << error.what() << '\n';
        return 1;
    }
}
