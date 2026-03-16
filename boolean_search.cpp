#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stack>

using namespace std;

struct DocMeta {
    uint32_t doc_id;
    string title;
    string path;
    uint32_t token_count;
};

struct VocabEntry {
    uint32_t df;
    uint64_t postings_offset;
    uint32_t postings_count;
};

struct Posting {
    uint32_t doc_id;
    uint32_t tf;
};

template <typename T>
bool read_binary(ifstream& in, T& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return static_cast<bool>(in);
}

static inline bool is_alnum_ascii(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

static inline bool is_digit_ascii(char c) {
    return std::isdigit(static_cast<unsigned char>(c)) != 0;
}

static inline char to_lower_ascii(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

vector<string> tokenize_text(const string& text) {
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

            if (has_alnum) tokens.push_back(current);
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

        if (has_alnum) tokens.push_back(current);
    }

    return tokens;
}

bool load_header(const string& path, uint32_t& doc_count, uint32_t& term_count) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    char magic[8];
    in.read(magic, 8);
    if (!in) return false;

    uint32_t version = 0;
    uint32_t reserved = 0;

    if (!read_binary(in, version)) return false;
    if (!read_binary(in, doc_count)) return false;
    if (!read_binary(in, term_count)) return false;
    if (!read_binary(in, reserved)) return false;

    return true;
}

bool load_forward(const string& path, vector<DocMeta>& docs, uint32_t doc_count) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    docs.resize(doc_count);

    for (uint32_t i = 0; i < doc_count; ++i) {
        uint32_t doc_id = 0, title_len = 0, path_len = 0, token_count = 0;
        if (!read_binary(in, doc_id)) return false;
        if (!read_binary(in, title_len)) return false;
        if (!read_binary(in, path_len)) return false;
        if (!read_binary(in, token_count)) return false;

        string title(title_len, '\0');
        string path_str(path_len, '\0');

        if (title_len > 0) in.read(&title[0], title_len);
        if (path_len > 0) in.read(&path_str[0], path_len);

        docs[doc_id] = {doc_id, title, path_str, token_count};
    }

    return true;
}

bool load_vocab(const string& path, unordered_map<string, VocabEntry>& vocab, uint32_t term_count) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    for (uint32_t i = 0; i < term_count; ++i) {
        uint32_t term_len = 0;
        if (!read_binary(in, term_len)) return false;

        string term(term_len, '\0');
        if (term_len > 0) in.read(&term[0], term_len);

        VocabEntry e{};
        uint32_t reserved = 0;

        if (!read_binary(in, e.df)) return false;
        if (!read_binary(in, e.postings_offset)) return false;
        if (!read_binary(in, e.postings_count)) return false;
        if (!read_binary(in, reserved)) return false;

        vocab[term] = e;
    }

    return true;
}

vector<uint32_t> read_postings_docs(const string& postings_path, const VocabEntry& e) {
    vector<uint32_t> docs;
    docs.reserve(e.postings_count);

    ifstream in(postings_path, ios::binary);
    if (!in) return docs;

    in.seekg(static_cast<std::streamoff>(e.postings_offset), ios::beg);

    for (uint32_t i = 0; i < e.postings_count; ++i) {
        uint32_t doc_id = 0;
        uint32_t tf = 0;
        if (!read_binary(in, doc_id)) break;
        if (!read_binary(in, tf)) break;
        docs.push_back(doc_id);
    }

    return docs;
}

vector<string> tokenize_query_expr(const string& query) {
    vector<string> out;
    string current;

    for (char raw : query) {
        char c = to_lower_ascii(raw);

        if (c == '(' || c == ')') {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            out.push_back(string(1, c));
            continue;
        }

        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) {
                out.push_back(current);
                current.clear();
            }
            continue;
        }

        current.push_back(c);
    }

    if (!current.empty()) out.push_back(current);

    return out;
}

bool is_operator_tok(const string& s) {
    return s == "and" || s == "or" || s == "not";
}

int precedence(const string& op) {
    if (op == "not") return 3;
    if (op == "and") return 2;
    if (op == "or") return 1;
    return 0;
}

vector<string> to_rpn(const vector<string>& tokens) {
    vector<string> output;
    vector<string> ops;

    for (const string& tok : tokens) {
        if (tok == "(") {
            ops.push_back(tok);
        } else if (tok == ")") {
            while (!ops.empty() && ops.back() != "(") {
                output.push_back(ops.back());
                ops.pop_back();
            }
            if (!ops.empty() && ops.back() == "(") ops.pop_back();
        } else if (is_operator_tok(tok)) {
            while (!ops.empty() && is_operator_tok(ops.back()) &&
                   precedence(ops.back()) >= precedence(tok)) {
                output.push_back(ops.back());
                ops.pop_back();
            }
            ops.push_back(tok);
        } else {
            vector<string> norm = tokenize_text(tok);
            for (const string& t : norm) output.push_back(t);
        }
    }

    while (!ops.empty()) {
        output.push_back(ops.back());
        ops.pop_back();
    }

    return output;
}

vector<uint32_t> set_union_docs(const vector<uint32_t>& a, const vector<uint32_t>& b) {
    vector<uint32_t> out;
    out.reserve(a.size() + b.size());

    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            out.push_back(a[i]);
            ++i; ++j;
        } else if (a[i] < b[j]) {
            out.push_back(a[i++]);
        } else {
            out.push_back(b[j++]);
        }
    }
    while (i < a.size()) out.push_back(a[i++]);
    while (j < b.size()) out.push_back(b[j++]);

    out.erase(unique(out.begin(), out.end()), out.end());
    return out;
}

vector<uint32_t> set_intersection_docs(const vector<uint32_t>& a, const vector<uint32_t>& b) {
    vector<uint32_t> out;
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i] == b[j]) {
            out.push_back(a[i]);
            ++i; ++j;
        } else if (a[i] < b[j]) {
            ++i;
        } else {
            ++j;
        }
    }
    return out;
}

vector<uint32_t> set_not_docs(const vector<uint32_t>& a, uint32_t doc_count) {
    vector<uint32_t> out;
    out.reserve(doc_count > a.size() ? doc_count - a.size() : 0);

    size_t j = 0;
    for (uint32_t d = 0; d < doc_count; ++d) {
        if (j < a.size() && a[j] == d) {
            ++j;
        } else {
            out.push_back(d);
        }
    }
    return out;
}

vector<uint32_t> eval_rpn(const vector<string>& rpn,
                          const unordered_map<string, VocabEntry>& vocab,
                          const string& postings_path,
                          uint32_t doc_count) {
    vector<vector<uint32_t>> st;

    for (const string& tok : rpn) {
        if (!is_operator_tok(tok)) {
            auto it = vocab.find(tok);
            if (it == vocab.end()) {
                st.push_back({});
            } else {
                st.push_back(read_postings_docs(postings_path, it->second));
            }
        } else if (tok == "not") {
            if (st.empty()) return {};
            vector<uint32_t> a = st.back();
            st.pop_back();
            st.push_back(set_not_docs(a, doc_count));
        } else if (tok == "and") {
            if (st.size() < 2) return {};
            vector<uint32_t> b = st.back(); st.pop_back();
            vector<uint32_t> a = st.back(); st.pop_back();
            st.push_back(set_intersection_docs(a, b));
        } else if (tok == "or") {
            if (st.size() < 2) return {};
            vector<uint32_t> b = st.back(); st.pop_back();
            vector<uint32_t> a = st.back(); st.pop_back();
            st.push_back(set_union_docs(a, b));
        }
    }

    if (st.empty()) return {};
    return st.back();
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./boolean_search <index_directory>\n";
        return 1;
    }

    string index_dir = argv[1];
    string header_path = index_dir + "/index_header.bin";
    string forward_path = index_dir + "/forward.bin";
    string vocab_path = index_dir + "/vocab.bin";
    string postings_path = index_dir + "/postings.bin";

    uint32_t doc_count = 0, term_count = 0;
    if (!load_header(header_path, doc_count, term_count)) {
        cerr << "Failed to load index header.\n";
        return 1;
    }

    vector<DocMeta> docs;
    unordered_map<string, VocabEntry> vocab;

    if (!load_forward(forward_path, docs, doc_count)) {
        cerr << "Failed to load forward index.\n";
        return 1;
    }

    if (!load_vocab(vocab_path, vocab, term_count)) {
        cerr << "Failed to load vocab.\n";
        return 1;
    }

    cerr << "Index loaded.\n";
    cerr << "Documents: " << doc_count << "\n";
    cerr << "Terms: " << term_count << "\n";

    string query;
    while (true) {
        cout << "Enter boolean query: ";
        if (!getline(cin, query)) break;
        if (query.empty()) break;

        vector<string> qtokens = tokenize_query_expr(query);
        vector<string> rpn = to_rpn(qtokens);
        vector<uint32_t> result_docs = eval_rpn(rpn, vocab, postings_path, doc_count);

        cout << "Results: " << result_docs.size() << "\n";
        size_t top_k = std::min<size_t>(10, result_docs.size());

        for (size_t i = 0; i < top_k; ++i) {
            uint32_t d = result_docs[i];
            cout << (i + 1) << ". doc_id=" << d
                 << " title=" << docs[d].title
                 << " path=" << docs[d].path
                 << "\n";
        }
    }

    return 0;
}
