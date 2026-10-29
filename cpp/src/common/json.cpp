/* =============================================================
   LOA Hybrid — JSON implementation
   ============================================================= */
#include "common/json.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace json {

// ─── Object access ────────────────────────────────────────────
bool Value::has(const std::string& k) const {
  if (t_ != Object) return false;
  for (const auto& kv : obj_) if (kv.first == k) return true;
  return false;
}

const Value& Value::get(const std::string& k) const {
  if (t_ == Object)
    for (const auto& kv : obj_) if (kv.first == k) return kv.second;
  throw std::runtime_error("JSON: missing key '" + k + "'");
}

Value& Value::set(const std::string& k, const Value& v) {
  t_ = Object;
  for (auto& kv : obj_) if (kv.first == k) { kv.second = v; return kv.second; }
  obj_.emplace_back(k, v);
  return obj_.back().second;
}

double Value::numOr(const std::string& k, double d) const {
  if (!has(k)) return d;
  const Value& v = get(k);
  return v.isNumber() ? v.n_ : d;
}

bool Value::boolOr(const std::string& k, bool d) const {
  if (!has(k)) return d;
  const Value& v = get(k);
  return v.t_ == Bool ? v.b_ : d;
}

std::string Value::strOr(const std::string& k, const std::string& d) const {
  if (!has(k)) return d;
  const Value& v = get(k);
  return v.isString() ? v.s_ : d;
}

// ─── Writer ───────────────────────────────────────────────────
static void escapeTo(std::string& out, const std::string& s) {
  out += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); out += b; }
        else out += static_cast<char>(c);
    }
  }
  out += '"';
}

void Value::dumpTo(std::string& out) const {
  switch (t_) {
    case Null:   out += "null"; break;
    case Bool:   out += b_ ? "true" : "false"; break;
    case Number: {
      // JSON has no Infinity / NaN: write null, the UI reads it as "∞ / none"
      if (!std::isfinite(n_)) { out += "null"; break; }
      char b[40];
      if (std::fabs(n_ - std::round(n_)) < 1e-9 && std::fabs(n_) < 9e15)
        std::snprintf(b, sizeof b, "%.0f", n_);
      else
        std::snprintf(b, sizeof b, "%.10g", n_);
      out += b;
      break;
    }
    case String: escapeTo(out, s_); break;
    case Array:
      out += '[';
      for (std::size_t i = 0; i < arr_.size(); i++) { if (i) out += ','; arr_[i].dumpTo(out); }
      out += ']';
      break;
    case Object:
      out += '{';
      for (std::size_t i = 0; i < obj_.size(); i++) {
        if (i) out += ',';
        escapeTo(out, obj_[i].first);
        out += ':';
        obj_[i].second.dumpTo(out);
      }
      out += '}';
      break;
  }
}

std::string Value::dump() const { std::string s; dumpTo(s); return s; }

// ─── Parser ───────────────────────────────────────────────────
namespace {

struct Parser {
  const std::string& s;
  std::size_t i = 0;

  explicit Parser(const std::string& text) : s(text) {}

  [[noreturn]] void fail(const char* what) {
    char b[96];
    std::snprintf(b, sizeof b, "JSON parse error at %u: %s", static_cast<unsigned>(i), what);
    throw std::runtime_error(b);
  }

  void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t')) i++; }

  bool lit(const char* w) {
    std::size_t n = 0; while (w[n]) n++;
    if (s.compare(i, n, w) == 0) { i += n; return true; }
    return false;
  }

  Value value() {
    ws();
    if (i >= s.size()) fail("unexpected end");
    char c = s[i];
    if (c == '{') return object();
    if (c == '[') return array();
    if (c == '"') return Value(string());
    if (lit("true"))  return Value(true);
    if (lit("false")) return Value(false);
    if (lit("null"))  return Value();
    if (c == '-' || (c >= '0' && c <= '9')) return number();
    fail("unexpected character");
  }

  Value number() {
    const char* start = s.c_str() + i;
    char* end = nullptr;
    double x = std::strtod(start, &end);
    if (end == start) fail("bad number");
    i += static_cast<std::size_t>(end - start);
    return Value(x);
  }

  std::string string() {
    i++; // opening quote
    std::string out;
    while (i < s.size() && s[i] != '"') {
      char c = s[i++];
      if (c != '\\') { out += c; continue; }
      if (i >= s.size()) fail("bad escape");
      char e = s[i++];
      switch (e) {
        case '"': out += '"'; break;   case '\\': out += '\\'; break;
        case '/': out += '/'; break;   case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;  case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;  case 't': out += '\t'; break;
        case 'u': {
          if (i + 4 > s.size()) fail("bad \\u escape");
          unsigned cp = static_cast<unsigned>(std::strtoul(s.substr(i, 4).c_str(), nullptr, 16));
          i += 4;
          // UTF-8 encode (BMP only; enough for names)
          if (cp < 0x80) out += static_cast<char>(cp);
          else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
          else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
          break;
        }
        default: fail("bad escape");
      }
    }
    if (i >= s.size()) fail("unterminated string");
    i++; // closing quote
    return out;
  }

  Value array() {
    i++;
    Value a = Value::array();
    ws();
    if (i < s.size() && s[i] == ']') { i++; return a; }
    for (;;) {
      a.push(value());
      ws();
      if (i < s.size() && s[i] == ',') { i++; continue; }
      if (i < s.size() && s[i] == ']') { i++; return a; }
      fail("expected , or ]");
    }
  }

  Value object() {
    i++;
    Value o = Value::object();
    ws();
    if (i < s.size() && s[i] == '}') { i++; return o; }
    for (;;) {
      ws();
      if (i >= s.size() || s[i] != '"') fail("expected key");
      std::string k = string();
      ws();
      if (i >= s.size() || s[i] != ':') fail("expected :");
      i++;
      o.set(k, value());
      ws();
      if (i < s.size() && s[i] == ',') { i++; continue; }
      if (i < s.size() && s[i] == '}') { i++; return o; }
      fail("expected , or }");
    }
  }
};

} // namespace

Value parse(const std::string& text) {
  Parser p(text);
  Value v = p.value();
  p.ws();
  if (p.i != text.size()) p.fail("trailing characters");
  return v;
}

} // namespace json
