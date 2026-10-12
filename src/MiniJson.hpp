#ifndef MINI_JSON_HPP
#define MINI_JSON_HPP

// Just enough JSON to read re/knowledge.json: objects, arrays, strings,
// numbers, true/false/null.
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct Json {
  enum class Kind { Null, Bool, Number, String, Array, Object } kind = Kind::Null;
  bool b = false;
  double n = 0;
  std::string s;
  std::vector<Json> items;
  std::map<std::string, Json> fields;

  const Json &operator[](const std::string &key) const {
    static const Json none;
    auto it = this->fields.find(key);
    return it == this->fields.end() ? none : it->second;
  }
  const Json &operator[](size_t i) const {
    static const Json none;
    return i < this->items.size() ? this->items[i] : none;
  }
  // (an array's length)
  size_t size() const { return this->items.size(); }
  std::string str(const std::string &fallback = "") const { return this->kind == Kind::String ? this->s : fallback; }
  double num(double fallback = 0) const { return this->kind == Kind::Number ? this->n : fallback; }
  bool truthy() const { return this->kind == Kind::Bool ? this->b : this->kind != Kind::Null; }

  static Json parse(const std::string &text) {
    size_t at = 0;
    Json j;
    value(text, at, j);
    return j;
  }

private:
  static void space(const std::string &t, size_t &at) {
    while (at < t.size() && (t[at] == ' ' || t[at] == '\n' || t[at] == '\r' || t[at] == '\t'))
      at++;
  }
  static void str(const std::string &t, size_t &at, std::string &out) {
    at++; // (the opening quote)
    while (at < t.size() && t[at] != '"') {
      char c = t[at++];
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at >= t.size())
        break;
      char e = t[at++];
      switch (e) {
      case 'n': out += '\n'; break;
      case 't': out += '\t'; break;
      case 'r': out += '\r'; break;
      case 'b': out += '\b'; break;
      case 'f': out += '\f'; break;
      case 'u': {
        unsigned cp = static_cast<unsigned>(std::strtoul(t.substr(at, 4).c_str(), nullptr, 16));
        at += 4;
        // (UTF-8; the knowledge base is ASCII-escaped, so these are rare)
        if (cp < 0x80)
          out += static_cast<char>(cp);
        else if (cp < 0x800) {
          out += static_cast<char>(0xC0 | (cp >> 6));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
          out += static_cast<char>(0xE0 | (cp >> 12));
          out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
          out += static_cast<char>(0x80 | (cp & 0x3F));
        }
        break;
      }
      default: out += e;
      }
    }
    at++; // (the closing quote)
  }
  static void value(const std::string &t, size_t &at, Json &j) {
    space(t, at);
    if (at >= t.size())
      return;
    char c = t[at];
    if (c == '{') {
      j.kind = Kind::Object;
      at++;
      for (;;) {
        space(t, at);
        if (at >= t.size() || t[at] == '}') {
          at++;
          return;
        }
        if (t[at] == ',') {
          at++;
          continue;
        }
        std::string key;
        str(t, at, key);
        space(t, at);
        at++; // ':'
        value(t, at, j.fields[key]);
      }
    } else if (c == '[') {
      j.kind = Kind::Array;
      at++;
      for (;;) {
        space(t, at);
        if (at >= t.size() || t[at] == ']') {
          at++;
          return;
        }
        if (t[at] == ',') {
          at++;
          continue;
        }
        j.items.emplace_back();
        value(t, at, j.items.back());
      }
    } else if (c == '"') {
      j.kind = Kind::String;
      str(t, at, j.s);
    } else if (c == 't' || c == 'f') {
      j.kind = Kind::Bool;
      j.b = c == 't';
      at += j.b ? 4 : 5;
    } else if (c == 'n') {
      at += 4;
    } else {
      j.kind = Kind::Number;
      char *end = nullptr;
      j.n = std::strtod(t.c_str() + at, &end);
      at = static_cast<size_t>(end - t.c_str());
    }
  }
};

#endif
