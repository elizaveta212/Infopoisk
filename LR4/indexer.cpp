#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <sstream>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

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

bool should_stem_token(const string& tok) {
    if (tok.size() < 3) return false;

    for (size_t i = 0; i < tok.size(); ++i) {
        char c = tok[i];
        if (std::isdigit(static_cast<unsigned char>(c))) return false;
        if (c == '-' || c == '+' || c == '\'') return false;
    }

    for (size_t i = 0; i < tok.size(); ++i) {
        if (!std::isalpha(static_cast<unsigned char>(tok[i]))) return false;
    }

    return true;
}

string normalize_term(const string& tok) {
    return tok;
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        cerr << "Usage: indexer <docs_directory> <output_directory>\n";
        return 1;
    }

    string docs_dir = argv[1];
    string out_dir = argv[2];

#ifdef _WIN32
    _mkdir(out_dir.c_str());
#else
    mkdir(out_dir.c_str(), 0777);
#endif

    vector<string> doc_paths;
    unordered_map<string, vector<pair<int, int> > > index;

    vector<string> all_files = list_txt_files(docs_dir);

    int doc_id = 0;

    for (size_t file_i = 0; file_i < all_files.size(); ++file_i) {
        string path = all_files[file_i];
        string text;

        if (!read_file(path, text)) {
            cerr << "Failed to read file: " << path << "\n";
            continue;
        }

        doc_paths.push_back(path);

        vector<string> tokens = tokenize(text);

        for (size_t i = 0; i < tokens.size(); ++i) {
            tokens[i] = normalize_term(tokens[i]);
        }

        unordered_map<string, int> tf;
        for (size_t i = 0; i < tokens.size(); ++i) {
            tf[tokens[i]]++;
        }

        for (unordered_map<string, int>::iterator it = tf.begin(); it != tf.end(); ++it) {
            index[it->first].push_back(make_pair(doc_id, it->second));
        }

        if ((doc_id + 1) % 1000 == 0) {
            cout << "Indexed documents: " << (doc_id + 1) << "\n";
        }

        doc_id++;
    }

    {
        ofstream out((out_dir + "/docs.txt").c_str());
        for (size_t i = 0; i < doc_paths.size(); ++i) {
            out << i << "\t" << doc_paths[i] << "\n";
        }
    }

    {
        ofstream dict_out((out_dir + "/dictionary.txt").c_str());
        ofstream post_out((out_dir + "/postings.txt").c_str());

        for (unordered_map<string, vector<pair<int, int> > >::iterator it = index.begin();
             it != index.end(); ++it) {
            const string& term = it->first;
            const vector<pair<int, int> >& postings = it->second;

            dict_out << term << "\t" << postings.size() << "\n";

            post_out << term;
            for (size_t j = 0; j < postings.size(); ++j) {
                post_out << "\t" << postings[j].first << ":" << postings[j].second;
            }
            post_out << "\n";
        }
    }

    cout << "Done.\n";
    cout << "Documents indexed: " << doc_paths.size() << "\n";
    cout << "Unique terms: " << index.size() << "\n";
    cout << "Output directory: " << out_dir << "\n";

    return 0;
}
