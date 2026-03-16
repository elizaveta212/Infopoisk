#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>

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

bool load_docs(const string& path, vector<string>& docs) {
    ifstream in(path.c_str());
    if (!in) return false;

    string line;
    while (getline(in, line)) {
        size_t tab = line.find('\t');
        if (tab == string::npos) continue;

        int doc_id = atoi(line.substr(0, tab).c_str());
        string doc_path = line.substr(tab + 1);

        if ((int)docs.size() <= doc_id) {
            docs.resize(doc_id + 1);
        }
        docs[doc_id] = doc_path;
    }

    return true;
}

bool load_postings(const string& path, unordered_map<string, vector<Posting> >& index) {
    ifstream in(path.c_str());
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

            int doc_id = atoi(item.substr(0, colon).c_str());
            int tf = atoi(item.substr(colon + 1).c_str());
            Posting p;
            p.doc_id = doc_id;
            p.tf = tf;
            postings.push_back(p);
        }

        index[term] = postings;
    }

    return true;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: search <index_directory>\n";
        return 1;
    }

    string index_dir = argv[1];
    string docs_path = index_dir + "/docs.txt";
    string postings_path = index_dir + "/postings.txt";

    vector<string> docs;
    unordered_map<string, vector<Posting> > index;

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
        getline(cin, query);
        if (!cin) break;
        if (query.empty()) break;

        vector<string> qtokens = tokenize(query);
        unordered_map<int, double> scores;

        for (size_t i = 0; i < qtokens.size(); ++i) {
            unordered_map<string, vector<Posting> >::iterator it = index.find(qtokens[i]);
            if (it == index.end()) continue;

            const vector<Posting>& postings = it->second;
            for (size_t j = 0; j < postings.size(); ++j) {
                scores[postings[j].doc_id] += postings[j].tf;
            }
        }

        vector<pair<int, double> > results;
        for (unordered_map<int, double>::iterator it = scores.begin(); it != scores.end(); ++it) {
            results.push_back(*it);
        }

        sort(results.begin(), results.end(),
             [](const pair<int, double>& a, const pair<int, double>& b) {
                 if (a.second != b.second) return a.second > b.second;
                 return a.first < b.first;
             });

        int top_k = (results.size() < 10) ? (int)results.size() : 10;

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