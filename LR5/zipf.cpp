#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <iomanip>

using namespace std;
namespace fs = std::filesystem;

static inline bool is_alnum_ascii(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

static inline bool is_digit_ascii(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

static inline char to_lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

vector<string> tokenize(const string& text) {
    vector<string> tokens;
    string current;
    size_t n = text.size();

    for (size_t i = 0; i < n; ++i) {
        char ch = to_lower_ascii(text[i]);
        char prev = (i > 0) ? to_lower_ascii(text[i - 1]) : '\0';
        char next = (i + 1 < n) ? to_lower_ascii(text[i + 1]) : '\0';

        if (is_alnum_ascii(ch)) {
            current.push_back(ch);
            continue;
        }

        if (ch == '-') {
            if (!current.empty() && next != '\0' && is_alnum_ascii(next)) {
                current.push_back(ch);
                continue;
            }
        }

        if (ch == '\'') {
            if (!current.empty() && next != '\0' && is_alnum_ascii(next)) {
                current.push_back(ch);
                continue;
            }
        }

        if (ch == '.') {
            if (!current.empty() && is_digit_ascii(prev) && is_digit_ascii(next)) {
                current.push_back(ch);
                continue;
            }
        }

        if (ch == '+') {
            if (!current.empty() && next == '+') {
                current.push_back(ch);
                continue;
            }
        }

        if (!current.empty()) {
            while (!current.empty()) {
                char back = current.back();
                if (back == '-' || back == '\'' || back == '+' || back == '.') {
                    current.pop_back();
                } else {
                    break;
                }
            }

            bool has_alnum = false;
            for (char c : current) {
                if (is_alnum_ascii(c)) {
                    has_alnum = true;
                    break;
                }
            }

            if (has_alnum) {
                tokens.push_back(current);
            }
            current.clear();
        }
    }

    if (!current.empty()) {
        while (!current.empty()) {
            char back = current.back();
            if (back == '-' || back == '\'' || back == '+' || back == '.') {
                current.pop_back();
            } else {
                break;
            }
        }

        bool has_alnum = false;
        for (char c : current) {
            if (is_alnum_ascii(c)) {
                has_alnum = true;
                break;
            }
        }

        if (has_alnum) {
            tokens.push_back(current);
        }
    }

    return tokens;
}

bool read_file(const string& path, string& content) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    in.seekg(0, ios::end);
    size_t size = static_cast<size_t>(in.tellg());
    in.seekg(0, ios::beg);

    content.resize(size);
    if (size > 0) {
        in.read(&content[0], size);
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./zipf <docs_directory> <output_prefix>\n";
        cerr << "Example: ./zipf /content/acl_fulltext_docs zipf\n";
        return 1;
    }

    string docs_dir = argv[1];
    string out_prefix = argv[2];

    unordered_map<string, long long> term_freq;
    long long total_tokens = 0;
    long long total_bytes = 0;
    long long file_count = 0;

    auto start = chrono::high_resolution_clock::now();

    for (const auto& entry : fs::directory_iterator(docs_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".txt") continue;

        string path = entry.path().string();
        string text;

        if (!read_file(path, text)) {
            cerr << "Failed to read file: " << path << "\n";
            continue;
        }

        total_bytes += static_cast<long long>(text.size());

        vector<string> tokens = tokenize(text);
        total_tokens += static_cast<long long>(tokens.size());

        for (const string& tok : tokens) {
            term_freq[tok]++;
        }

        file_count++;
        if (file_count % 1000 == 0) {
            cout << "Processed files: " << file_count << "\n";
        }
    }

    vector<long long> freqs;
    freqs.reserve(term_freq.size());
    for (const auto& kv : term_freq) {
        freqs.push_back(kv.second);
    }

    sort(freqs.begin(), freqs.end(), greater<long long>());

    size_t n = freqs.size();
    if (n == 0) {
        cerr << "No terms found.\n";
        return 1;
    }

    double C = static_cast<double>(freqs[0]);

    // Подгонка log(freq) = a + b*log(rank)
    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;

    for (size_t i = 0; i < n; ++i) {
        double rank = static_cast<double>(i + 1);
        double freq = static_cast<double>(freqs[i]);

        double x = std::log(rank);
        double y = std::log(freq);

        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }

    double N = static_cast<double>(n);
    double denom = N * sum_xx - sum_x * sum_x;
    double b = (N * sum_xy - sum_x * sum_y) / denom;
    double a = (sum_y - b * sum_x) / N;

    double s_fit = -b;
    double C_fit = std::exp(a);

    string csv_path = out_prefix + "_data.csv";
    ofstream csv(csv_path);
    csv << "rank,real_freq,zipf_ideal,zipf_fit\n";

    for (size_t i = 0; i < n; ++i) {
        double rank = static_cast<double>(i + 1);
        double real_freq = static_cast<double>(freqs[i]);
        double zipf_ideal = C / rank;
        double zipf_fit = C_fit / std::pow(rank, s_fit);

        csv << (i + 1) << ","
            << fixed << setprecision(10)
            << real_freq << ","
            << zipf_ideal << ","
            << zipf_fit << "\n";
    }
    csv.close();

    string gp_path = out_prefix + "_plot.gp";
    ofstream gp(gp_path);
    gp << "set terminal pngcairo size 1400,900\n";
    gp << "set output '" << out_prefix << "_plot.png'\n";
    gp << "set datafile separator ','\n";
    gp << "set logscale xy\n";
    gp << "set xlabel 'Term rank (log scale)'\n";
    gp << "set ylabel 'Term frequency (log scale)'\n";
    gp << "set title 'Term frequency distribution and Zipf law'\n";
    gp << "set grid\n";
    gp << "plot \\\n";
    gp << "'" << csv_path << "' using 1:2 with lines title 'Real data', \\\n";
    gp << "'" << csv_path << "' using 1:3 with lines title 'Zipf s=1', \\\n";
    gp << "'" << csv_path << "' using 1:4 with lines title 'Fitted Zipf'\n";
    gp.close();

    auto finish = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = finish - start;

    cout << fixed << setprecision(6);
    cout << "\nDone.\n";
    cout << "Files processed: " << file_count << "\n";
    cout << "Total tokens: " << total_tokens << "\n";
    cout << "Unique terms: " << n << "\n";
    cout << "Input size (bytes): " << total_bytes << "\n";
    cout << "Execution time (sec): " << elapsed.count() << "\n";
    cout << "Top term frequency C: " << C << "\n";
    cout << "Fitted s: " << s_fit << "\n";
    cout << "Fitted C: " << C_fit << "\n";
    cout << "CSV saved to: " << csv_path << "\n";
    cout << "Gnuplot script saved to: " << gp_path << "\n";
    cout << "PNG output will be: " << out_prefix << "_plot.png\n";

    return 0;
}
