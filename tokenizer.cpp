
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <cctype>
#include <iomanip>

using namespace std;

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
    if (argc != 2) {
        cerr << "Usage: ./tokenizer <docs_directory>\n";
        return 1;
    }

    string docs_dir = argv[1];

    size_t total_tokens = 0;
    size_t total_token_length = 0;
    size_t total_input_bytes = 0;
    size_t file_count = 0;

    auto start = chrono::high_resolution_clock::now();

    for (const auto& entry : fs::directory_iterator(docs_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".txt") continue;

        string text;
        if (!read_file(entry.path().string(), text)) {
            cerr << "Failed to read file: " << entry.path() << "\n";
            continue;
        }

        total_input_bytes += text.size();

        vector<string> toks = tokenize(text);

        total_tokens += toks.size();
        for (const string& tok : toks) {
            total_token_length += tok.size();
        }

        file_count++;
    }

    auto finish = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = finish - start;

    double avg_token_length = (total_tokens > 0)
        ? static_cast<double>(total_token_length) / static_cast<double>(total_tokens)
        : 0.0;

    double input_kb = static_cast<double>(total_input_bytes) / 1024.0;
    double kb_per_sec = (elapsed.count() > 0.0) ? input_kb / elapsed.count() : 0.0;
    double sec_per_kb = (input_kb > 0.0) ? elapsed.count() / input_kb : 0.0;
    double tokens_per_sec = (elapsed.count() > 0.0)
        ? static_cast<double>(total_tokens) / elapsed.count()
        : 0.0;

    cout << fixed << setprecision(6);
    cout << "Files processed: " << file_count << "\n";
    cout << "Total tokens: " << total_tokens << "\n";
    cout << "Average token length: " << avg_token_length << "\n";
    cout << "Input size (bytes): " << total_input_bytes << "\n";
    cout << "Input size (KB): " << input_kb << "\n";
    cout << "Execution time (sec): " << elapsed.count() << "\n";
    cout << "Tokenization speed (tokens/sec): " << tokens_per_sec << "\n";
    cout << "Tokenization speed (KB/sec): " << kb_per_sec << "\n";
    cout << "Time per 1 KB (sec/KB): " << sec_per_kb << "\n";

    return 0;
}