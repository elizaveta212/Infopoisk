#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "porter_stemmer.h"

using namespace std;

struct Posting {
    int doc_id;
    int tf;
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

bool should_stem_token(const string& tok) {
    if (tok.size() < 3) return false;

    for (char c : tok) {
        if (std::isdigit(static_cast<unsigned char>(c))) return false;
        if (c == '-' || c == '+' || c == '\'') return false;
    }

    for (char c : tok) {
        if (!std::isalpha(static_cast<unsigned char>(c))) return false;
    }

    return true;
}

string normalize_term(const string& tok) {
    if (should_stem_token(tok)) {
        return PorterStemmer::stem(tok);
    }
    return tok;
}

bool load_docs(const string& path, vector<string>& docs) {
    ifstream in(path);
    if (!in) return false;

    string line;
    while (getline(in, line)) {
        size_t tab = line.find('\t');
        if (tab == string::npos) continue;

        int doc_id = stoi(line.substr(0, tab));
        string doc_path = line.substr(tab + 1);

        if ((int)docs.size() <= doc_id) {
            docs.resize(doc_id + 1);
        }
        docs[doc_id] = doc_path;
    }

    return true;
}

bool load_postings(const string& path, unordered_map<string, vector<Posting>>& index) {
    ifstream in(path);
    if (!in) return false;

    string line;
    while (getline(in, line)) {
        if (line.empty()) continue;

        stringstream ss(line);
        string term;
        if (!getline(ss, term, '\t')) continue;

        vector<Posting> postings;
        string item;

        while (getline(ss, item, '\t')) {
            size_t colon = item.find(':');
            if (colon == string::npos) continue;

            int doc_id = stoi(item.substr(0, colon));
            int tf = stoi(item.substr(colon + 1));
            postings.push_back({doc_id, tf});
        }

        index[term] = postings;
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./search <index_directory>\n";
        return 1;
    }

    string index_dir = argv[1];
    string docs_path = index_dir + "/docs.txt";
    string postings_path = index_dir + "/postings.txt";

    vector<string> docs;
    unordered_map<string, vector<Posting>> index;

    if (!load_docs(docs_path, docs)) {
        cerr << "Failed to load docs.txt\n";
        return 1;
    }

    if (!load_postings(postings_path, index)) {
        cerr << "Failed to load postings.txt\n";
        return 1;
    }

    cerr << "Index loaded.\n";
    cerr << "Documents: " << docs.size() << "\n";
    cerr << "Terms: " << index.size() << "\n";

    string query;
    while (true) {
        cout << "Enter query: ";
        if (!getline(cin, query)) break;
        if (query.empty()) break;

        vector<string> qtokens = tokenize(query);

        for (string& tok : qtokens) {
            tok = normalize_term(tok);
        }

        unordered_map<int, double> scores;

        for (const string& tok : qtokens) {
            auto it = index.find(tok);
            if (it == index.end()) continue;

            for (const Posting& p : it->second) {
                scores[p.doc_id] += p.tf;
            }
        }

        vector<pair<int, double>> results;
        results.reserve(scores.size());
        for (const auto& kv : scores) {
            results.push_back(kv);
        }

        sort(results.begin(), results.end(),
             [](const auto& a, const auto& b) {
                 if (a.second != b.second) return a.second > b.second;
                 return a.first < b.first;
             });

        int top_k = min(10, (int)results.size());

        cout << "Results: " << top_k << "\n";
        for (int i = 0; i < top_k; ++i) {
            int doc_id = results[i].first;
            double score = results[i].second;

            cout << (i + 1) << ". "
                 << "doc_id=" << doc_id
                 << " score=" << score
                 << " path=" << docs[doc_id]
                 << "\n";
        }

        if (top_k == 0) {
            cout << "No results.\n";
        }
    }

    return 0;
}
