from flask import Flask, request, Response
import os
import sys
import struct
import html

app = Flask(__name__)

DOCS = []
VOCAB = {}
POSTINGS_PATH = None
DOC_COUNT = 0
TERM_COUNT = 0


def read_u32(f):
    data = f.read(4)
    if len(data) != 4:
        raise EOFError
    return struct.unpack("<I", data)[0]


def read_u64(f):
    data = f.read(8)
    if len(data) != 8:
        raise EOFError
    return struct.unpack("<Q", data)[0]


def load_header(path):
    global DOC_COUNT, TERM_COUNT
    with open(path, "rb") as f:
        magic = f.read(8)
        if len(magic) != 8:
            raise RuntimeError("Bad header")
        version = read_u32(f)
        DOC_COUNT = read_u32(f)
        TERM_COUNT = read_u32(f)
        reserved = read_u32(f)


def load_forward(path):
    global DOCS
    docs = [None] * DOC_COUNT
    with open(path, "rb") as f:
        for _ in range(DOC_COUNT):
            doc_id = read_u32(f)
            title_len = read_u32(f)
            path_len = read_u32(f)
            token_count = read_u32(f)

            title = f.read(title_len).decode("utf-8", errors="replace") if title_len else ""
            doc_path = f.read(path_len).decode("utf-8", errors="replace") if path_len else ""

            docs[doc_id] = {
                "doc_id": doc_id,
                "title": title,
                "path": doc_path,
                "token_count": token_count,
            }
    DOCS = docs


def load_vocab(path):
    global VOCAB
    vocab = {}
    with open(path, "rb") as f:
        for _ in range(TERM_COUNT):
            term_len = read_u32(f)
            term = f.read(term_len).decode("utf-8", errors="replace") if term_len else ""
            df = read_u32(f)
            postings_offset = read_u64(f)
            postings_count = read_u32(f)
            reserved = read_u32(f)

            vocab[term] = {
                "df": df,
                "postings_offset": postings_offset,
                "postings_count": postings_count,
            }
    VOCAB = vocab


def read_postings_docs(entry):
    docs = []
    with open(POSTINGS_PATH, "rb") as f:
        f.seek(entry["postings_offset"])
        for _ in range(entry["postings_count"]):
            doc_id = read_u32(f)
            tf = read_u32(f)
            docs.append(doc_id)
    return docs


def is_alnum_ascii(c):
    return c.isalnum()


def tokenize_text(text):
    text = text.lower()
    tokens = []
    current = []
    n = len(text)

    for i, ch in enumerate(text):
        prev_char = text[i - 1] if i > 0 else ""
        next_char = text[i + 1] if i + 1 < n else ""

        if is_alnum_ascii(ch):
            current.append(ch)
            continue

        if ch == "-":
            if current and next_char and is_alnum_ascii(next_char):
                current.append(ch)
                continue

        if ch == "'":
            if current and next_char and is_alnum_ascii(next_char):
                current.append(ch)
                continue

        if ch == ".":
            if current and prev_char.isdigit() and next_char.isdigit():
                current.append(ch)
                continue

        if ch == "+":
            if current and next_char == "+":
                current.append(ch)
                continue

        if current:
            token = "".join(current).strip("-'+.")
            if token and any(c.isalnum() for c in token):
                tokens.append(token)
            current = []

    if current:
        token = "".join(current).strip("-'+.")
        if token and any(c.isalnum() for c in token):
            tokens.append(token)

    return tokens


def is_operator_tok(s):
    return s in ("AND", "OR", "NOT")


def precedence(op):
    if op == "NOT":
        return 3
    if op == "AND":
        return 2
    if op == "OR":
        return 1
    return 0


def is_term_like(s):
    return s not in ("(", ")", "AND", "OR", "NOT")


def split_query_lexemes(query):
    out = []
    cur = []

    def flush():
        nonlocal cur
        if cur:
            out.append("".join(cur))
            cur = []

    i = 0
    while i < len(query):
        c = query[i]

        if c.isspace():
            flush()
            i += 1
            continue

        if c in "()":
            flush()
            out.append(c)
            i += 1
            continue

        if c == "!":
            flush()
            out.append("NOT")
            i += 1
            continue

        if c == "&" and i + 1 < len(query) and query[i + 1] == "&":
            flush()
            out.append("AND")
            i += 2
            continue

        if c == "|" and i + 1 < len(query) and query[i + 1] == "|":
            flush()
            out.append("OR")
            i += 2
            continue

        cur.append(c.lower())
        i += 1

    flush()
    return out


def normalize_query_terms(raw_tokens):
    out = []
    for tok in raw_tokens:
        if tok in ("(", ")", "AND", "OR", "NOT"):
            out.append(tok)
        else:
            out.extend(tokenize_text(tok))
    return out


def insert_implicit_and(tokens):
    if not tokens:
        return []

    out = [tokens[0]]
    for i in range(1, len(tokens)):
        prev_tok = tokens[i - 1]
        cur_tok = tokens[i]

        prev_can_end = is_term_like(prev_tok) or prev_tok == ")"
        cur_can_start = is_term_like(cur_tok) or cur_tok == "(" or cur_tok == "NOT"

        if prev_can_end and cur_can_start:
            out.append("AND")
        out.append(cur_tok)

    return out


def to_rpn(tokens):
    output = []
    ops = []

    for tok in tokens:
        if tok == "(":
            ops.append(tok)
        elif tok == ")":
            while ops and ops[-1] != "(":
                output.append(ops.pop())
            if ops and ops[-1] == "(":
                ops.pop()
        elif is_operator_tok(tok):
            while ops and is_operator_tok(ops[-1]) and precedence(ops[-1]) >= precedence(tok):
                output.append(ops.pop())
            ops.append(tok)
        else:
            output.append(tok)

    while ops:
        output.append(ops.pop())
    return output


def set_union_docs(a, b):
    out = []
    i = j = 0
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            out.append(a[i])
            i += 1
            j += 1
        elif a[i] < b[j]:
            out.append(a[i])
            i += 1
        else:
            out.append(b[j])
            j += 1
    out.extend(a[i:])
    out.extend(b[j:])

    dedup = []
    prev = None
    for x in out:
        if prev is None or x != prev:
            dedup.append(x)
        prev = x
    return dedup


def set_intersection_docs(a, b):
    out = []
    i = j = 0
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            out.append(a[i])
            i += 1
            j += 1
        elif a[i] < b[j]:
            i += 1
        else:
            j += 1
    return out


def set_not_docs(a, doc_count):
    out = []
    j = 0
    for d in range(doc_count):
        if j < len(a) and a[j] == d:
            j += 1
        else:
            out.append(d)
    return out


def eval_rpn(rpn):
    st = []

    for tok in rpn:
        if not is_operator_tok(tok):
            entry = VOCAB.get(tok)
            if entry is None:
                st.append([])
            else:
                st.append(read_postings_docs(entry))
        elif tok == "NOT":
            if not st:
                return []
            a = st.pop()
            st.append(set_not_docs(a, DOC_COUNT))
        elif tok == "AND":
            if len(st) < 2:
                return []
            b = st.pop()
            a = st.pop()
            st.append(set_intersection_docs(a, b))
        elif tok == "OR":
            if len(st) < 2:
                return []
            b = st.pop()
            a = st.pop()
            st.append(set_union_docs(a, b))

    return st[-1] if st else []


def run_query(query):
    raw = split_query_lexemes(query)
    norm = normalize_query_terms(raw)
    expanded = insert_implicit_and(norm)
    rpn = to_rpn(expanded)
    result_docs = eval_rpn(rpn)
    return result_docs


def html_page(title, body):
    return f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>{html.escape(title)}</title>
<style>
body {{
    font-family: Arial, sans-serif;
    max-width: 1000px;
    margin: 40px auto;
    padding: 0 20px;
    line-height: 1.5;
}}
form {{
    margin-bottom: 24px;
}}
input[type=text] {{
    width: 70%;
    padding: 10px;
    font-size: 16px;
}}
button {{
    padding: 10px 14px;
    font-size: 16px;
}}
.result {{
    margin-bottom: 18px;
    padding-bottom: 12px;
    border-bottom: 1px solid #ddd;
}}
.meta {{
    color: #666;
    font-size: 14px;
}}
</style>
</head>
<body>
{body}
</body>
</html>"""


@app.route("/")
def home():
    body = """
    <h1>Boolean Search</h1>
    <form action="/search" method="get">
        <input type="text" name="q" placeholder="Enter boolean query">
        <button type="submit">Search</button>
    </form>
    <p>Examples:</p>
    <ul>
        <li>machine translation</li>
        <li>(question || answering) !speech</li>
        <li>named entity recognition</li>
    </ul>
    """
    return html_page("Boolean Search", body)


@app.route("/search")
def search():
    query = request.args.get("q", "")
    page = request.args.get("page", "1")

    try:
        page = max(1, int(page))
    except ValueError:
        page = 1

    per_page = 50
    start = (page - 1) * per_page
    end = start + per_page

    results = run_query(query) if query.strip() else []
    page_results = results[start:end]

    body = f"""
    <h1>Search results</h1>
    <form action="/search" method="get">
        <input type="text" name="q" value="{html.escape(query)}" placeholder="Enter boolean query">
        <button type="submit">Search</button>
    </form>
    <p>Found: {len(results)}</p>
    """

    for doc_id in page_results:
        doc = DOCS[doc_id]
        title = html.escape(doc["title"] or "(no title)")
        path = html.escape(doc["path"])
        body += f"""
        <div class="result">
            <div><a href="{path}" target="_blank">{title}</a></div>
            <div class="meta">doc_id={doc_id}</div>
            <div class="meta">{path}</div>
        </div>
        """

    if end < len(results):
        next_page = page + 1
        body += f'<p><a href="/search?q={html.escape(query)}&page={next_page}">Next 50 results</a></p>'

    return html_page("Search results", body)


def load_index(index_dir):
    global POSTINGS_PATH
    header_path = os.path.join(index_dir, "index_header.bin")
    forward_path = os.path.join(index_dir, "forward.bin")
    vocab_path = os.path.join(index_dir, "vocab.bin")
    POSTINGS_PATH = os.path.join(index_dir, "postings.bin")

    load_header(header_path)
    load_forward(forward_path)
    load_vocab(vocab_path)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python search_web.py <index_directory>")
        sys.exit(1)

    index_dir = sys.argv[1]
    load_index(index_dir)
    app.run(host="127.0.0.1", port=5000, debug=False)
