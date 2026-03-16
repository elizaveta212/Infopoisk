%%writefile poisk.cpp
#include <thread>
#include <chrono>

#include <libxml/HTMLparser.h>
#include <libxml/uri.h>
#include <libxml/tree.h>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <set>
#include <ctime>
#include <regex>
#include <algorithm>
#include <cctype>
#include <sstream>

#include <sqlite3.h>
#include <curl/curl.h>
#include <yaml-cpp/yaml.h>

using std::cerr;
using std::cout;
using std::endl;
using std::set;
using std::string;
using std::vector;

struct Config {
    string db_path;
    int crawl_delay_seconds;
    int recrawl_interval_seconds;
    int request_timeout_seconds;
    int max_pages_per_run;
    long max_html_bytes;
    string user_agent;
    vector<string> allowed_domains;
    vector<string> seed_urls;
};

struct HttpResponse {
    long status_code = 0;
    string body;
    string content_type;
    string etag;
    string last_modified;
    bool too_large = false;
    bool request_failed = false;
};

static size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    HttpResponse* resp = static_cast<HttpResponse*>(userdata);
    size_t total = size * nmemb;
    resp->body.append(ptr, total);
    return total;
}

static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    HttpResponse* resp = static_cast<HttpResponse*>(userdata);
    size_t total = size * nitems;
    string line(buffer, total);

    auto lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower.rfind("content-type:", 0) == 0) {
        resp->content_type = line.substr(13);
        while (!resp->content_type.empty() &&
               (resp->content_type.back() == '\r' || resp->content_type.back() == '\n' || std::isspace((unsigned char)resp->content_type.back()))) {
            resp->content_type.pop_back();
        }
        while (!resp->content_type.empty() && std::isspace((unsigned char)resp->content_type.front())) {
            resp->content_type.erase(resp->content_type.begin());
        }
    } else if (lower.rfind("etag:", 0) == 0) {
        resp->etag = line.substr(5);
        while (!resp->etag.empty() &&
               (resp->etag.back() == '\r' || resp->etag.back() == '\n' || std::isspace((unsigned char)resp->etag.back()))) {
            resp->etag.pop_back();
        }
        while (!resp->etag.empty() && std::isspace((unsigned char)resp->etag.front())) {
            resp->etag.erase(resp->etag.begin());
        }
    } else if (lower.rfind("last-modified:", 0) == 0) {
        resp->last_modified = line.substr(14);
        while (!resp->last_modified.empty() &&
               (resp->last_modified.back() == '\r' || resp->last_modified.back() == '\n' || std::isspace((unsigned char)resp->last_modified.back()))) {
            resp->last_modified.pop_back();
        }
        while (!resp->last_modified.empty() && std::isspace((unsigned char)resp->last_modified.front())) {
            resp->last_modified.erase(resp->last_modified.begin());
        }
    }

    return total;
}

static string to_lower_copy(const string& s) {
    string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

static bool starts_with(const string& s, const string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

static string trim(const string& s) {
    size_t l = 0;
    while (l < s.size() && std::isspace((unsigned char)s[l])) l++;
    size_t r = s.size();
    while (r > l && std::isspace((unsigned char)s[r - 1])) r--;
    return s.substr(l, r - l);
}

static string get_host(const string& url) {
    std::regex re(R"(^(https?)://([^/]+))", std::regex::icase);
    std::smatch m;
    if (std::regex_search(url, m, re)) {
        return to_lower_copy(m[2].str());
    }
    return "";
}

static string get_path_part(const string& url) {
    std::regex re(R"(^(https?)://[^/]+(.*)$)", std::regex::icase);
    std::smatch m;
    if (std::regex_search(url, m, re)) {
        string p = m[2].str();
        if (p.empty()) return "/";
        return p;
    }
    return "/";
}

static string normalize_url(const string& raw) {
    string url = trim(raw);
    if (url.empty()) return "";

    if (!starts_with(to_lower_copy(url), "http://") && !starts_with(to_lower_copy(url), "https://")) {
        return "";
    }

    size_t hash_pos = url.find('#');
    if (hash_pos != string::npos) url = url.substr(0, hash_pos);

    size_t query_pos = url.find('?');
    if (query_pos != string::npos) url = url.substr(0, query_pos);

    std::regex re(R"(^(https?)://([^/]+)(/?.*)$)", std::regex::icase);
    std::smatch m;
    if (!std::regex_match(url, m, re)) return "";

    string scheme = to_lower_copy(m[1].str());
    string host = to_lower_copy(m[2].str());
    string path = m[3].str();

    if (path.empty()) path = "/";
    if (path.size() > 1 && path.back() == '/') path.pop_back();

    return scheme + "://" + host + path;
}

static string get_source_name(const string& url) {
    return get_host(url);
}

static bool is_allowed_domain(const string& url, const vector<string>& allowed_domains) {
    string host = get_host(url);
    for (const auto& d0 : allowed_domains) {
        string d = to_lower_copy(d0);
        if (host == d) return true;
        if (host.size() > d.size() && host.compare(host.size() - d.size(), d.size(), d) == 0 &&
            host[host.size() - d.size() - 1] == '.') {
            return true;
        }
    }
    return false;
}

static bool is_useful_url(const string& url) {
    if (url.empty()) return false;
    if (url.size() > 180) return false;

    string path = to_lower_copy(get_path_part(url));

    if (path.size() >= 4 && path.substr(path.size() - 4) == ".pdf") return false;

    const vector<string> bad_prefixes = {
        "/join", "/login", "/logout", "/signup", "/settings",
        "/notifications", "/tasks", "/spaces", "/models", "/docs", "/api"
    };

    const vector<string> bad_substrings = {
        "/discussions/", "/blob/", "/tree/", "/resolve/", "/commits/", "/oauth"
    };

    for (const auto& p : bad_prefixes) {
        if (starts_with(path, p)) return false;
    }
    for (const auto& s : bad_substrings) {
        if (path.find(s) != string::npos) return false;
    }

    return true;
}

static string join_url(const string& base, const string& href) {
    if (href.empty()) return "";

    string h = trim(href);
    if (h.empty()) return "";

    xmlChar* built = xmlBuildURI(
        BAD_CAST h.c_str(),
        BAD_CAST base.c_str()
    );

    if (!built) return "";

    string result = reinterpret_cast<const char*>(built);
    xmlFree(built);

    return normalize_url(result);
}

static void collect_links_from_node(xmlNode* node,
                                    const string& base_url,
                                    const vector<string>& allowed_domains,
                                    set<string>& out_links) {
    for (xmlNode* cur = node; cur != nullptr; cur = cur->next) {
        if (cur->type == XML_ELEMENT_NODE) {
            if (xmlStrcasecmp(cur->name, BAD_CAST "a") == 0) {
                xmlChar* href = xmlGetProp(cur, BAD_CAST "href");
                if (href) {
                    string href_str = reinterpret_cast<const char*>(href);
                    xmlFree(href);

                    string abs = join_url(base_url, href_str);
                    if (!abs.empty() &&
                        is_allowed_domain(abs, allowed_domains) &&
                        is_useful_url(abs)) {
                        out_links.insert(abs);
                    }
                }
            }
        }

        if (cur->children) {
            collect_links_from_node(cur->children, base_url, allowed_domains, out_links);
        }
    }
}

static vector<string> extract_links(const string& base_url,
                                    const string& html,
                                    const vector<string>& allowed_domains) {
    set<string> links;

    htmlDocPtr doc = htmlReadMemory(
        html.c_str(),
        static_cast<int>(html.size()),
        base_url.c_str(),
        "UTF-8",
        HTML_PARSE_NOERROR | HTML_PARSE_NOWARNING | HTML_PARSE_RECOVER
    );

    if (!doc) {
        return vector<string>();
    }

    xmlNode* root = xmlDocGetRootElement(doc);
    if (root) {
        collect_links_from_node(root, base_url, allowed_domains, links);
    }

    xmlFreeDoc(doc);

    return vector<string>(links.begin(), links.end());
}


static string simple_hash(const string& s) {
    std::hash<string> h;
    size_t v = h(s);
    std::ostringstream oss;
    oss << v;
    return oss.str();
}

static bool exec_sql(sqlite3* db, const string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        cerr << "SQLite error: " << (err ? err : "unknown") << endl;
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

static bool init_db(sqlite3* db) {
    string sql1 = R"(
        CREATE TABLE IF NOT EXISTS documents (
            url TEXT PRIMARY KEY,
            html TEXT NOT NULL,
            source TEXT NOT NULL,
            crawl_timestamp INTEGER NOT NULL,
            etag TEXT,
            last_modified TEXT,
            content_hash TEXT,
            status_code INTEGER
        );
    )";

    string sql2 = R"(
        CREATE TABLE IF NOT EXISTS frontier (
            url TEXT PRIMARY KEY,
            status TEXT NOT NULL,
            discovered_from TEXT,
            fail_count INTEGER NOT NULL DEFAULT 0,
            last_crawled_at INTEGER,
            next_crawl_at INTEGER NOT NULL,
            created_at INTEGER NOT NULL,
            picked_at INTEGER
        );
    )";

    string sql3 = R"(
        CREATE INDEX IF NOT EXISTS idx_frontier_status_next
        ON frontier(status, next_crawl_at);
    )";

    return exec_sql(db, sql1) && exec_sql(db, sql2) && exec_sql(db, sql3);
}

static bool seed_frontier(sqlite3* db, const vector<string>& seed_urls, const vector<string>& allowed_domains) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO frontier
        (url, status, discovered_from, fail_count, last_crawled_at, next_crawl_at, created_at, picked_at)
        VALUES (?, 'pending', NULL, 0, NULL, ?, ?, NULL);
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    long now = std::time(nullptr);

    for (const auto& raw : seed_urls) {
        string url = normalize_url(raw);
        if (url.empty()) continue;
        if (!is_allowed_domain(url, allowed_domains)) continue;
        if (!is_useful_url(url)) continue;

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, now);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
    return true;
}

static void reset_stale_in_progress(sqlite3* db) {
    exec_sql(db, "UPDATE frontier SET status='pending' WHERE status='in_progress';");
}

static bool pick_next_url(sqlite3* db, string& out_url) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        SELECT url
        FROM frontier
        WHERE status IN ('pending', 'done', 'failed')
          AND next_crawl_at <= ?
        ORDER BY next_crawl_at ASC, created_at ASC
        LIMIT 1;
    )";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    long now = std::time(nullptr);
    sqlite3_bind_int64(stmt, 1, now);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    out_url = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);

    sqlite3_stmt* upd = nullptr;
    const char* upd_sql = "UPDATE frontier SET status='in_progress', picked_at=? WHERE url=?;";
    sqlite3_prepare_v2(db, upd_sql, -1, &upd, nullptr);
    sqlite3_bind_int64(upd, 1, now);
    sqlite3_bind_text(upd, 2, out_url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(upd);
    sqlite3_finalize(upd);

    return true;
}

static bool get_document_meta(sqlite3* db, const string& url, string& etag, string& last_modified, string& old_hash) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT etag, last_modified, content_hash FROM documents WHERE url=?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    const unsigned char* e = sqlite3_column_text(stmt, 0);
    const unsigned char* l = sqlite3_column_text(stmt, 1);
    const unsigned char* h = sqlite3_column_text(stmt, 2);

    etag = e ? reinterpret_cast<const char*>(e) : "";
    last_modified = l ? reinterpret_cast<const char*>(l) : "";
    old_hash = h ? reinterpret_cast<const char*>(h) : "";

    sqlite3_finalize(stmt);
    return true;
}

static void mark_frontier_done(sqlite3* db, const string& url, int recrawl_interval_seconds) {
    long now = std::time(nullptr);
    long next_crawl = now + recrawl_interval_seconds;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE frontier SET status='done', last_crawled_at=?, next_crawl_at=? WHERE url=?;";
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_int64(stmt, 2, next_crawl);
    sqlite3_bind_text(stmt, 3, url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void mark_frontier_failed(sqlite3* db, const string& url, int recrawl_interval_seconds) {
    long now = std::time(nullptr);
    long next_crawl = now + recrawl_interval_seconds;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        UPDATE frontier
        SET status='failed',
            fail_count=fail_count+1,
            last_crawled_at=?,
            next_crawl_at=?
        WHERE url=?;
    )";
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_int64(stmt, 2, next_crawl);
    sqlite3_bind_text(stmt, 3, url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void add_discovered_links(sqlite3* db, const string& discovered_from, const vector<string>& links) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT OR IGNORE INTO frontier
        (url, status, discovered_from, fail_count, last_crawled_at, next_crawl_at, created_at, picked_at)
        VALUES (?, 'pending', ?, 0, NULL, ?, ?, NULL);
    )";

    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    long now = std::time(nullptr);

    for (const auto& url : links) {
        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, discovered_from.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 3, now);
        sqlite3_bind_int64(stmt, 4, now);
        sqlite3_step(stmt);
    }

    sqlite3_finalize(stmt);
}

static bool upsert_document(sqlite3* db, const string& url, const string& html, const string& source,
                            long crawl_timestamp, const HttpResponse& resp, bool& changed) {
    string etag, last_modified, old_hash;
    bool exists = get_document_meta(db, url, etag, last_modified, old_hash);

    string content_hash = simple_hash(html);
    changed = (!exists || old_hash != content_hash);

    if (changed) {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            INSERT INTO documents
            (url, html, source, crawl_timestamp, etag, last_modified, content_hash, status_code)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(url) DO UPDATE SET
                html=excluded.html,
                source=excluded.source,
                crawl_timestamp=excluded.crawl_timestamp,
                etag=excluded.etag,
                last_modified=excluded.last_modified,
                content_hash=excluded.content_hash,
                status_code=excluded.status_code;
        )";
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, html.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, crawl_timestamp);
        sqlite3_bind_text(stmt, 5, resp.etag.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, resp.last_modified.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, content_hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 8, (int)resp.status_code);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    } else {
        sqlite3_stmt* stmt = nullptr;
        const char* sql = R"(
            UPDATE documents
            SET crawl_timestamp=?, etag=?, last_modified=?, status_code=?
            WHERE url=?;
        )";
        sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        sqlite3_bind_int64(stmt, 1, crawl_timestamp);
        sqlite3_bind_text(stmt, 2, resp.etag.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, resp.last_modified.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, (int)resp.status_code);
        sqlite3_bind_text(stmt, 5, url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    return true;
}

static HttpResponse fetch_url(const string& url, const Config& cfg, const string& etag, const string& last_modified) {
    HttpResponse resp;
    CURL* curl = curl_easy_init();
    if (!curl) {
        resp.request_failed = true;
        return resp;
    }

    struct curl_slist* headers = nullptr;
    if (!etag.empty()) {
        headers = curl_slist_append(headers, ("If-None-Match: " + etag).c_str());
    }
    if (!last_modified.empty()) {
        headers = curl_slist_append(headers, ("If-Modified-Since: " + last_modified).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, cfg.user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, cfg.request_timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &resp);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        resp.request_failed = true;
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.status_code);
    }

    if ((long)resp.body.size() > cfg.max_html_bytes) {
        resp.too_large = true;
    }

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return resp;
}

static string process_url(sqlite3* db, const Config& cfg, const string& url) {
    string old_etag, old_last_modified, old_hash;
    get_document_meta(db, url, old_etag, old_last_modified, old_hash);

    HttpResponse resp = fetch_url(url, cfg, old_etag, old_last_modified);

    if (resp.request_failed) {
        mark_frontier_failed(db, url, cfg.recrawl_interval_seconds);
        return "failed_request";
    }

    if (resp.status_code == 304) {
        mark_frontier_done(db, url, cfg.recrawl_interval_seconds);
        return "not_modified";
    }

    if (resp.status_code != 200) {
        mark_frontier_failed(db, url, cfg.recrawl_interval_seconds);
        return "failed_http_" + std::to_string(resp.status_code);
    }

    string ctype = to_lower_copy(resp.content_type);
    if (ctype.find("text/html") == string::npos) {
        mark_frontier_failed(db, url, cfg.recrawl_interval_seconds);
        return "skipped_content_type_" + ctype;
    }

    if (resp.too_large) {
        mark_frontier_failed(db, url, cfg.recrawl_interval_seconds);
        return "skipped_too_large";
    }

    bool changed = false;
    upsert_document(db, url, resp.body, get_source_name(url), std::time(nullptr), resp, changed);

    vector<string> links = extract_links(url, resp.body, cfg.allowed_domains);
    add_discovered_links(db, url, links);
    mark_frontier_done(db, url, cfg.recrawl_interval_seconds);

    return changed ? "changed" : "unchanged";
}

static Config load_config(const string& path) {
    YAML::Node cfg = YAML::LoadFile(path);
    Config c;

    c.db_path = cfg["db"]["path"].as<string>();
    c.crawl_delay_seconds = cfg["logic"]["crawl_delay_seconds"].as<int>();
    c.recrawl_interval_seconds = cfg["logic"]["recrawl_interval_seconds"].as<int>();
    c.request_timeout_seconds = cfg["logic"]["request_timeout_seconds"].as<int>();
    c.max_pages_per_run = cfg["logic"]["max_pages_per_run"].as<int>();
    c.max_html_bytes = cfg["logic"]["max_html_bytes"].as<long>();
    c.user_agent = cfg["logic"]["user_agent"].as<string>();

    for (const auto& x : cfg["logic"]["allowed_domains"]) {
        c.allowed_domains.push_back(x.as<string>());
    }

    for (const auto& x : cfg["logic"]["seed_urls"]) {
        c.seed_urls.push_back(x.as<string>());
    }

    return c;
}
static void print_table_count(sqlite3* db, const string& table_name) {
    string sql = "SELECT COUNT(*) FROM " + table_name + ";";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare count query for " << table_name << endl;
        return;
    }

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        long long cnt = sqlite3_column_int64(stmt, 0);
        cout << table_name << " count: " << cnt << endl;
    }

    sqlite3_finalize(stmt);
}

static void print_frontier_status_stats(sqlite3* db) {
    const char* sql = R"(
        SELECT status, COUNT(*)
        FROM frontier
        GROUP BY status
        ORDER BY status;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare frontier status stats query" << endl;
        return;
    }

    cout << "\nFrontier status stats:\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* status = sqlite3_column_text(stmt, 0);
        long long cnt = sqlite3_column_int64(stmt, 1);

        cout << "  "
             << (status ? reinterpret_cast<const char*>(status) : "")
             << " -> " << cnt << endl;
    }

    sqlite3_finalize(stmt);
}

static void print_documents_preview(sqlite3* db, int limit = 10) {
    const char* sql = R"(
        SELECT url, source, crawl_timestamp, status_code
        FROM documents
        ORDER BY crawl_timestamp DESC
        LIMIT ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare documents preview query" << endl;
        return;
    }

    sqlite3_bind_int(stmt, 1, limit);

    cout << "\nDocuments preview:\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* url = sqlite3_column_text(stmt, 0);
        const unsigned char* source = sqlite3_column_text(stmt, 1);
        long long ts = sqlite3_column_int64(stmt, 2);
        int status_code = sqlite3_column_int(stmt, 3);

        cout << "  url: " << (url ? reinterpret_cast<const char*>(url) : "") << "\n";
        cout << "  source: " << (source ? reinterpret_cast<const char*>(source) : "") << "\n";
        cout << "  crawl_timestamp: " << ts << "\n";
        cout << "  status_code: " << status_code << "\n\n";
    }

    sqlite3_finalize(stmt);
}

static void print_frontier_preview(sqlite3* db, int limit = 10) {
    const char* sql = R"(
        SELECT url, status, fail_count, last_crawled_at, next_crawl_at
        FROM frontier
        ORDER BY next_crawl_at ASC
        LIMIT ?;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare frontier preview query" << endl;
        return;
    }

    sqlite3_bind_int(stmt, 1, limit);

    cout << "\nFrontier preview:\n";
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* url = sqlite3_column_text(stmt, 0);
        const unsigned char* status = sqlite3_column_text(stmt, 1);
        int fail_count = sqlite3_column_int(stmt, 2);
        long long last_crawled_at = sqlite3_column_int64(stmt, 3);
        long long next_crawl_at = sqlite3_column_int64(stmt, 4);

        cout << "  url: " << (url ? reinterpret_cast<const char*>(url) : "") << "\n";
        cout << "  status: " << (status ? reinterpret_cast<const char*>(status) : "") << "\n";
        cout << "  fail_count: " << fail_count << "\n";
        cout << "  last_crawled_at: " << last_crawled_at << "\n";
        cout << "  next_crawl_at: " << next_crawl_at << "\n\n";
    }

    sqlite3_finalize(stmt);
}

static void print_one_document_html_preview(sqlite3* db) {
    const char* sql = R"(
        SELECT url, html
        FROM documents
        LIMIT 1;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        cerr << "Failed to prepare html preview query" << endl;
        return;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* url = sqlite3_column_text(stmt, 0);
        const unsigned char* html = sqlite3_column_text(stmt, 1);

        string html_str = html ? reinterpret_cast<const char*>(html) : "";
        if (html_str.size() > 300) {
            html_str = html_str.substr(0, 300);
        }

        cout << "\nOne document HTML preview:\n";
        cout << "  url: " << (url ? reinterpret_cast<const char*>(url) : "") << "\n";
        cout << "  html[0:300]: " << html_str << "\n";
    }

    sqlite3_finalize(stmt);
}

static void print_db_summary(sqlite3* db) {
    cout << "\n=== DATABASE SUMMARY ===\n";
    print_table_count(db, "documents");
    print_table_count(db, "frontier");
    print_frontier_status_stats(db);
    print_documents_preview(db, 10);
    print_frontier_preview(db, 10);
    print_one_document_html_preview(db);
    cout << "=== END OF DATABASE SUMMARY ===\n";
}


int main(int argc, char* argv[]) {
    if (argc != 2) {
        cerr << "Usage: ./crawler config.yaml\n";
        return 1;
    }

    Config cfg = load_config(argv[1]);

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        cerr << "curl init failed\n";
        return 1;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(cfg.db_path.c_str(), &db) != SQLITE_OK) {
        cerr << "Cannot open database\n";
        curl_global_cleanup();
        return 1;
    }

    if (!init_db(db)) {
        sqlite3_close(db);
        curl_global_cleanup();
        return 1;
    }

    seed_frontier(db, cfg.seed_urls, cfg.allowed_domains);
    reset_stale_in_progress(db);

    int processed = 0;
    while (processed < cfg.max_pages_per_run) {
        string url;
        if (!pick_next_url(db, url)) {
            cout << "No URLs ready for crawling.\n";
            break;
        }

        string result = process_url(db, cfg, url);
        processed++;
        cout << "[" << processed << "] " << url << " -> " << result << endl;

        std::this_thread::sleep_for(std::chrono::seconds(cfg.crawl_delay_seconds));
    }

    cout << "Done. Processed: " << processed << endl;
    print_db_summary(db);
    sqlite3_close(db);
    curl_global_cleanup();
    return 0;
}
