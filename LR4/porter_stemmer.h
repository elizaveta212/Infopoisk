#ifndef PORTER_STEMMER_H
#define PORTER_STEMMER_H

#include <string>
#include <cctype>

class PorterStemmer {
public:
    static std::string stem(const std::string& input) {
        if (input.size() <= 2) return input;

        std::string w = input;
        for (char& c : w) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (!is_alpha_word(w)) return input;

        step1a(w);
        step1b(w);
        step1c(w);
        step2(w);
        step3(w);
        step4(w);
        step5a(w);
        step5b(w);

        return w;
    }

private:
    static bool is_alpha_word(const std::string& s) {
        for (char c : s) {
            if (!std::isalpha(static_cast<unsigned char>(c))) return false;
        }
        return true;
    }

    static bool is_consonant(const std::string& s, int i) {
        char ch = s[i];
        switch (ch) {
            case 'a': case 'e': case 'i': case 'o': case 'u':
                return false;
            case 'y':
                return (i == 0) ? true : !is_consonant(s, i - 1);
            default:
                return true;
        }
    }

    static int measure(const std::string& s) {
        int n = 0;
        bool in_vowel_seq = false;
        for (int i = 0; i < (int)s.size(); ++i) {
            bool v = !is_consonant(s, i);
            if (v) {
                in_vowel_seq = true;
            } else {
                if (in_vowel_seq) {
                    n++;
                    in_vowel_seq = false;
                }
            }
        }
        return n;
    }

    static bool contains_vowel(const std::string& s) {
        for (int i = 0; i < (int)s.size(); ++i) {
            if (!is_consonant(s, i)) return true;
        }
        return false;
    }

    static bool ends_with_double_consonant(const std::string& s) {
        int n = (int)s.size();
        if (n < 2) return false;
        if (s[n - 1] != s[n - 2]) return false;
        return is_consonant(s, n - 1);
    }

    static bool cvc(const std::string& s) {
        int n = (int)s.size();
        if (n < 3) return false;
        if (!is_consonant(s, n - 1) || is_consonant(s, n - 2) || !is_consonant(s, n - 3)) return false;
        char ch = s[n - 1];
        return !(ch == 'w' || ch == 'x' || ch == 'y');
    }

    static bool ends_with(const std::string& s, const std::string& suffix) {
        if (suffix.size() > s.size()) return false;
        return s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    static void replace_suffix(std::string& s, const std::string& suffix, const std::string& repl) {
        s.replace(s.size() - suffix.size(), suffix.size(), repl);
    }

    static std::string stem_part(const std::string& s, const std::string& suffix) {
        return s.substr(0, s.size() - suffix.size());
    }

    static void step1a(std::string& s) {
        if (ends_with(s, "sses")) replace_suffix(s, "sses", "ss");
        else if (ends_with(s, "ies")) replace_suffix(s, "ies", "i");
        else if (ends_with(s, "ss")) {}
        else if (ends_with(s, "s")) s.pop_back();
    }

    static void step1b(std::string& s) {
        if (ends_with(s, "eed")) {
            std::string stem = stem_part(s, "eed");
            if (measure(stem) > 0) replace_suffix(s, "eed", "ee");
        } else if (ends_with(s, "ed")) {
            std::string stem = stem_part(s, "ed");
            if (contains_vowel(stem)) {
                s = stem;
                step1b_helper(s);
            }
        } else if (ends_with(s, "ing")) {
            std::string stem = stem_part(s, "ing");
            if (contains_vowel(stem)) {
                s = stem;
                step1b_helper(s);
            }
        }
    }

    static void step1b_helper(std::string& s) {
        if (ends_with(s, "at") || ends_with(s, "bl") || ends_with(s, "iz")) {
            s += "e";
        } else if (ends_with_double_consonant(s)) {
            char ch = s.back();
            if (!(ch == 'l' || ch == 's' || ch == 'z')) {
                s.pop_back();
            }
        } else if (measure(s) == 1 && cvc(s)) {
            s += "e";
        }
    }

    static void step1c(std::string& s) {
        if (ends_with(s, "y")) {
            std::string stem = stem_part(s, "y");
            if (contains_vowel(stem)) {
                s[s.size() - 1] = 'i';
            }
        }
    }

    static void step2(std::string& s) {
        static const std::pair<const char*, const char*> rules[] = {
            {"ational", "ate"}, {"tional", "tion"}, {"enci", "ence"}, {"anci", "ance"},
            {"izer", "ize"}, {"abli", "able"}, {"alli", "al"}, {"entli", "ent"},
            {"eli", "e"}, {"ousli", "ous"}, {"ization", "ize"}, {"ation", "ate"},
            {"ator", "ate"}, {"alism", "al"}, {"iveness", "ive"}, {"fulness", "ful"},
            {"ousness", "ous"}, {"aliti", "al"}, {"iviti", "ive"}, {"biliti", "ble"}
        };

        for (const auto& r : rules) {
            std::string suf = r.first;
            if (ends_with(s, suf)) {
                std::string stem = stem_part(s, suf);
                if (measure(stem) > 0) {
                    replace_suffix(s, suf, r.second);
                }
                return;
            }
        }
    }

    static void step3(std::string& s) {
        static const std::pair<const char*, const char*> rules[] = {
            {"icate", "ic"}, {"ative", ""}, {"alize", "al"},
            {"iciti", "ic"}, {"ical", "ic"}, {"ful", ""}, {"ness", ""}
        };

        for (const auto& r : rules) {
            std::string suf = r.first;
            if (ends_with(s, suf)) {
                std::string stem = stem_part(s, suf);
                if (measure(stem) > 0) {
                    replace_suffix(s, suf, r.second);
                }
                return;
            }
        }
    }

    static void step4(std::string& s) {
        static const char* suffixes[] = {
            "al", "ance", "ence", "er", "ic", "able", "ible", "ant", "ement",
            "ment", "ent", "ion", "ou", "ism", "ate", "iti", "ous", "ive", "ize"
        };

        for (const char* suf_c : suffixes) {
            std::string suf = suf_c;
            if (ends_with(s, suf)) {
                std::string stem = stem_part(s, suf);
                if (suf == "ion") {
                    if (!stem.empty()) {
                        char ch = stem.back();
                        if ((ch == 's' || ch == 't') && measure(stem) > 1) {
                            s = stem;
                        }
                    }
                } else {
                    if (measure(stem) > 1) {
                        s = stem;
                    }
                }
                return;
            }
        }
    }

    static void step5a(std::string& s) {
        if (ends_with(s, "e")) {
            std::string stem = stem_part(s, "e");
            int m = measure(stem);
            if (m > 1 || (m == 1 && !cvc(stem))) {
                s = stem;
            }
        }
    }

    static void step5b(std::string& s) {
        if (measure(s) > 1 && ends_with_double_consonant(s) && s.back() == 'l') {
            s.pop_back();
        }
    }
};

#endif
