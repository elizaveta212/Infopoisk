#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

using namespace std;
namespace fs = std::filesystem;

struct Posting {
    uint32_t doc_id;
    uint32_t tf;
};

struct DocMeta {
    uint32_t doc_id;
    string title;
    string path;
    uint32_t token_count;
};

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

string extract_title(const string& text) {
    size_t pos = text.find("Title:");
    if (pos == string::npos) return "";

    pos += 6;
    while (pos < text.size() && std::isspace((unsigned char)text[pos])) pos++;

    size_t end = text.find('\n', pos);
    if (end == string::npos) end = text.size();

    return text.substr(pos, end - pos);
}

template <typename T>
void write_binary(ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: ./indexer_binary <docs_directory> <output_directory>\n";
        return 1;
    }

    string docs_dir = argv[1];
    string out_dir = argv[2];

    fs::create_directories(out_dir);

    vector<DocMeta> docs;
    unordered_map<string, vector<Posting>> index;

    uint64_t total_input_bytes = 0;
    uint64_t total_tokens = 0;
    uint64_t total_term_length = 0;

    auto start = chrono::high_resolution_clock::now();

    uint32_t doc_id = 0;

    for (const auto& entry : fs::directory_iterator(docs_dir)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".txt") continue;

        string path = entry.path().string();
        string text;

        if (!read_file(path, text)) {
            cerr << "Failed to read file: " << path << "\n";
            continue;
        }

        total_input_bytes += text.size();

        string title = extract_title(text);
        vector<string> tokens = tokenize(text);
        total_tokens += tokens.size();

        unordered_map<string, uint32_t> tf;
        for (const string& tok : tokens) {
            tf[tok]++;
        }

        for (const auto& kv : tf) {
            index[kv.first].push_back({doc_id, kv.second});
        }

        docs.push_back({doc_id, title, path, static_cast<uint32_t>(tokens.size())});

        if ((doc_id + 1) % 1000 == 0) {
            cout << "Indexed documents: " << (doc_id + 1) << "\n";
        }

        doc_id++;
    }

    vector<pair<string, vector<Posting>>> terms;
    terms.reserve(index.size());

    for (auto& kv : index) {
        total_term_length += kv.first.size();
        terms.push_back(std::move(kv));
    }

    sort(terms.begin(), terms.end(),
         [](const auto& a, const auto& b) {
             return a.first < b.first;
         });

    ofstream header_out(out_dir + "/index_header.bin", ios::binary);
    ofstream forward_out(out_dir + "/forward.bin", ios::binary);
    ofstream vocab_out(out_dir + "/vocab.bin", ios::binary);
    ofstream postings_out(out_dir + "/postings.bin", ios::binary);

    if (!header_out || !forward_out || !vocab_out || !postings_out) {
        cerr << "Failed to open output files.\n";
        return 1;
    }

  
    char magic[8] = {'I','D','X','V','0','0','0','1'};
    header_out.write(magic, 8);

    uint32_t version = 1;
    uint32_t doc_count = static_cast<uint32_t>(docs.size());
    uint32_t term_count = static_cast<uint32_t>(terms.size());
    uint32_t reserved = 0;

    write_binary(header_out, version);
    write_binary(header_out, doc_count);
    write_binary(header_out, term_count);
    write_binary(header_out, reserved);

    for (const auto& doc : docs) {
        write_binary(forward_out, doc.doc_id);

        uint32_t title_len = static_cast<uint32_t>(doc.title.size());
        uint32_t path_len = static_cast<uint32_t>(doc.path.size());

        write_binary(forward_out, title_len);
        write_binary(forward_out, path_len);
        write_binary(forward_out, doc.token_count);

        if (title_len > 0) forward_out.write(doc.title.data(), title_len);
        if (path_len > 0) forward_out.write(doc.path.data(), path_len);
    }

  
    for (const auto& item : terms) {
        const string& term = item.first;
        const vector<Posting>& postings = item.second;

        uint64_t postings_offset = static_cast<uint64_t>(postings_out.tellp());

        for (const auto& p : postings) {
            write_binary(postings_out, p.doc_id);
            write_binary(postings_out, p.tf);
        }

        uint32_t term_len = static_cast<uint32_t>(term.size());
        uint32_t df = static_cast<uint32_t>(postings.size());
        uint32_t postings_count = static_cast<uint32_t>(postings.size());
        uint32_t vocab_reserved = 0;

        write_binary(vocab_out, term_len);
        if (term_len > 0) vocab_out.write(term.data(), term_len);
        write_binary(vocab_out, df);
        write_binary(vocab_out, postings_offset);
        write_binary(vocab_out, postings_count);
        write_binary(vocab_out, vocab_reserved);
    }

    auto finish = chrono::high_resolution_clock::now();
    chrono::duration<double> elapsed = finish - start;

    double avg_term_length = terms.empty()
        ? 0.0
        : static_cast<double>(total_term_length) / static_cast<double>(terms.size());

    double docs_per_sec = elapsed.count() > 0.0
        ? static_cast<double>(docs.size()) / elapsed.count()
        : 0.0;

    double kb = static_cast<double>(total_input_bytes) / 1024.0;
    double kb_per_sec = elapsed.count() > 0.0
        ? kb / elapsed.count()
        : 0.0;

    double sec_per_doc = docs.empty()
        ? 0.0
        : elapsed.count() / static_cast<double>(docs.size());

    cout << "\nDone.\n";
    cout << "Documents indexed: " << docs.size() << "\n";
    cout << "Terms count: " << terms.size() << "\n";
    cout << "Average term length: " << avg_term_length << "\n";
    cout << "Input size (bytes): " << total_input_bytes << "\n";
    cout << "Execution time (sec): " << elapsed.count() << "\n";
    cout << "Indexing speed (docs/sec): " << docs_per_sec << "\n";
    cout << "Time per document (sec/doc): " << sec_per_doc << "\n";
    cout << "Indexing speed (KB/sec): " << kb_per_sec << "\n";
    cout << "Output dir: " << out_dir << "\n";

    return 0;
}
