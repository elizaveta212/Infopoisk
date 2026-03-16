#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

using namespace std;

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
            for (size_t j = 0; j < current.size(); ++j) {
                if (is_alnum_ascii(current[j])) {
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
        for (size_t j = 0; j < current.size(); ++j) {
            if (is_alnum_ascii(current[j])) {
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
    ifstream in(path.c_str(), ios::binary);
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

bool ends_with_txt(const string& s) {
    if (s.size() < 4) return false;
    return s.substr(s.size() - 4) == ".txt";
}

vector<string> list_txt_files(const string& dir_path) {
    vector<string> files;

#ifdef _WIN32
    string pattern = dir_path + "\\*.txt";
    WIN32_FIND_DATAA data;
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &data);

    if (hFind == INVALID_HANDLE_VALUE) {
        return files;
    }

    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            files.push_back(dir_path + "\\" + data.cFileName);
        }
    } while (FindNextFileA(hFind, &data));

    FindClose(hFind);
#else
    DIR* dir = opendir(dir_path.c_str());
    if (!dir) return files;

    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        string name = entry->d_name;
        if (name == "." || name == "..") continue;
        if (!ends_with_txt(name)) continue;
        files.push_back(dir_path + "/" + name);
    }

    closedir(dir);
#endif

    sort(files.begin(), files.end());
    return files;
}

string extract_title(const string& text) {
    size_t pos = text.find("Title:");
    if (pos == string::npos) return "";

    pos += 6;
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) pos++;

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
        cerr << "Usage: indexer_binary <docs_directory> <output_directory>\n";
        return 1;
    }

    string docs_dir = argv[1];
    string out_dir = argv[2];

#ifdef _WIN32
    _mkdir(out_dir.c_str());
#else
    mkdir(out_dir.c_str(), 0777);
#endif

    vector<DocMeta> docs;
    unordered_map<string, vector<Posting> > index;

    uint64_t total_input_bytes = 0;
    uint64_t total_term_length = 0;

    auto start = chrono::high_resolution_clock::now();

    vector<string> all_files = list_txt_files(docs_dir);

    uint32_t doc_id = 0;

    for (size_t file_i = 0; file_i < all_files.size(); ++file_i) {
        string path = all_files[file_i];
        string text;

        if (!read_file(path, text)) {
            cerr << "Failed to read file: " << path << "\n";
            continue;
        }

        total_input_bytes += static_cast<uint64_t>(text.size());

        string title = extract_title(text);
        vector<string> tokens = tokenize(text);

        unordered_map<string, uint32_t> tf;
        for (size_t i = 0; i < tokens.size(); ++i) {
            tf[tokens[i]]++;
        }

        for (unordered_map<string, uint32_t>::iterator it = tf.begin(); it != tf.end(); ++it) {
            Posting p;
            p.doc_id = doc_id;
            p.tf = it->second;
            index[it->first].push_back(p);
        }

        DocMeta dm;
        dm.doc_id = doc_id;
        dm.title = title;
        dm.path = path;
        dm.token_count = static_cast<uint32_t>(tokens.size());
        docs.push_back(dm);

        if ((doc_id + 1) % 1000 == 0) {
            cout << "Indexed documents: " << (doc_id + 1) << "\n";
        }

        doc_id++;
    }

    vector<pair<string, vector<Posting> > > terms;
    terms.reserve(index.size());

    for (unordered_map<string, vector<Posting> >::iterator it = index.begin(); it != index.end(); ++it) {
        total_term_length += static_cast<uint64_t>(it->first.size());
        terms.push_back(*it);
    }

    sort(terms.begin(), terms.end(),
         [](const pair<string, vector<Posting> >& a, const pair<string, vector<Posting> >& b) {
             return a.first < b.first;
         });

    ofstream header_out((out_dir + "/index_header.bin").c_str(), ios::binary);
    ofstream forward_out((out_dir + "/forward.bin").c_str(), ios::binary);
    ofstream vocab_out((out_dir + "/vocab.bin").c_str(), ios::binary);
    ofstream postings_out((out_dir + "/postings.bin").c_str(), ios::binary);

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

    for (size_t i = 0; i < docs.size(); ++i) {
        write_binary(forward_out, docs[i].doc_id);

        uint32_t title_len = static_cast<uint32_t>(docs[i].title.size());
        uint32_t path_len = static_cast<uint32_t>(docs[i].path.size());

        write_binary(forward_out, title_len);
        write_binary(forward_out, path_len);
        write_binary(forward_out, docs[i].token_count);

        if (title_len > 0) forward_out.write(docs[i].title.data(), title_len);
        if (path_len > 0) forward_out.write(docs[i].path.data(), path_len);
    }

    for (size_t i = 0; i < terms.size(); ++i) {
        const string& term = terms[i].first;
        const vector<Posting>& postings = terms[i].second;

        uint64_t postings_offset = static_cast<uint64_t>(postings_out.tellp());

        for (size_t j = 0; j < postings.size(); ++j) {
            write_binary(postings_out, postings[j].doc_id);
            write_binary(postings_out, postings[j].tf);
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