#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <algorithm>

using namespace std;

struct DocMeta {
    uint32_t doc_id;
    string title;
    string path;
    uint32_t token_count;
};

struct VocabEntry {
    string term;
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

bool load_header(const string& path, uint32_t& version, uint32_t& doc_count, uint32_t& term_count) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    char magic[8];
    in.read(magic, 8);
    if (!in) return false;

    uint32_t reserved = 0;
    if (!read_binary(in, version)) return false;
    if (!read_binary(in, doc_count)) return false;
    if (!read_binary(in, term_count)) return false;
    if (!read_binary(in, reserved)) return false;

    cout << "Magic: ";
    for (int i = 0; i < 8; ++i) cout << magic[i];
    cout << "\nVersion: " << version
         << "\nDocuments: " << doc_count
         << "\nTerms: " << term_count
         << "\n";
    return true;
}

bool load_forward(const string& path, vector<DocMeta>& docs) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    while (true) {
        uint32_t doc_id = 0, title_len = 0, path_len = 0, token_count = 0;
        if (!read_binary(in, doc_id)) break;
        if (!read_binary(in, title_len)) return false;
        if (!read_binary(in, path_len)) return false;
        if (!read_binary(in, token_count)) return false;

        string title(title_len, '\0');
        string path_str(path_len, '\0');

        if (title_len > 0) in.read(&title[0], title_len);
        if (path_len > 0) in.read(&path_str[0], path_len);

        docs.push_back({doc_id, title, path_str, token_count});
    }

    return true;
}

bool load_vocab(const string& path, vector<VocabEntry>& vocab) {
    ifstream in(path, ios::binary);
    if (!in) return false;

    while (true) {
        uint32_t term_len = 0;
        if (!read_binary(in, term_len)) break;

        string term(term_len, '\0');
        if (term_len > 0) in.read(&term[0], term_len);

        VocabEntry e{};
        uint32_t reserved = 0;

        if (!read_binary(in, e.df)) return false;
        if (!read_binary(in, e.postings_offset)) return false;
        if (!read_binary(in, e.postings_count)) return false;
        if (!read_binary(in, reserved)) return false;

        e.term = term;
        vocab.push_back(e);
    }

    return true;
}

vector<Posting> read_postings(const string& postings_path, const VocabEntry& e) {
    vector<Posting> out;
    out.reserve(e.postings_count);

    ifstream in(postings_path, ios::binary);
    if (!in) return out;

    in.seekg(static_cast<std::streamoff>(e.postings_offset), ios::beg);

    for (uint32_t i = 0; i < e.postings_count; ++i) {
        Posting p{};
        if (!read_binary(in, p.doc_id)) break;
        if (!read_binary(in, p.tf)) break;
        out.push_back(p);
    }

    return out;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./dump_index <index_directory>\n";
        return 1;
    }

    string index_dir = argv[1];
    string header_path = index_dir + "/index_header.bin";
    string forward_path = index_dir + "/forward.bin";
    string vocab_path = index_dir + "/vocab.bin";
    string postings_path = index_dir + "/postings.bin";

    uint32_t version = 0, doc_count = 0, term_count = 0;
    if (!load_header(header_path, version, doc_count, term_count)) {
        cerr << "Failed to read header\n";
        return 1;
    }

    vector<DocMeta> docs;
    if (!load_forward(forward_path, docs)) {
        cerr << "Failed to read forward index\n";
        return 1;
    }

    vector<VocabEntry> vocab;
    if (!load_vocab(vocab_path, vocab)) {
        cerr << "Failed to read vocab\n";
        return 1;
    }

    cout << "\n=== FIRST DOCUMENTS ===\n";
    for (size_t i = 0; i < min<size_t>(5, docs.size()); ++i) {
        cout << "doc_id=" << docs[i].doc_id
             << " token_count=" << docs[i].token_count
             << "\n  title=" << docs[i].title
             << "\n  path=" << docs[i].path << "\n";
    }

    cout << "\n=== FIRST TERMS ===\n";
    for (size_t i = 0; i < min<size_t>(5, vocab.size()); ++i) {
        const auto& e = vocab[i];
        cout << "term=" << e.term
             << " df=" << e.df
             << " postings_offset=" << e.postings_offset
             << " postings_count=" << e.postings_count << "\n";

        vector<Posting> ps = read_postings(postings_path, e);
        cout << "  postings:";
        for (size_t j = 0; j < min<size_t>(5, ps.size()); ++j) {
            cout << " (" << ps[j].doc_id << "," << ps[j].tf << ")";
        }
        cout << "\n";
    }

    return 0;
}
