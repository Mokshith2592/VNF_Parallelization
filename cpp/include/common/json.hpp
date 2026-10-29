/* =============================================================
   LOA Hybrid — Minimal JSON value, parser and writer
   Enough JSON for the config the UI sends and the report it reads.
   Objects keep insertion order so the report reads top-down.
   ============================================================= */
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

namespace json {

class Value {
public:
  enum Type { Null, Bool, Number, String, Array, Object };

  Value() : t_(Null) {}
  Value(std::nullptr_t) : t_(Null) {}
  Value(bool b) : t_(Bool), b_(b) {}
  Value(int x) : t_(Number), n_(x) {}
  Value(long x) : t_(Number), n_(static_cast<double>(x)) {}
  Value(long long x) : t_(Number), n_(static_cast<double>(x)) {}
  Value(unsigned int x) : t_(Number), n_(static_cast<double>(x)) {}
  Value(unsigned long x) : t_(Number), n_(static_cast<double>(x)) {}
  Value(unsigned long long x) : t_(Number), n_(static_cast<double>(x)) {}
  Value(double x) : t_(Number), n_(x) {}
  Value(const char* s) : t_(String), s_(s) {}
  Value(const std::string& s) : t_(String), s_(s) {}

  static Value array()  { Value v; v.t_ = Array;  return v; }
  static Value object() { Value v; v.t_ = Object; return v; }

  Type type() const { return t_; }
  bool isNull()   const { return t_ == Null; }
  bool isNumber() const { return t_ == Number; }
  bool isString() const { return t_ == String; }
  bool isArray()  const { return t_ == Array; }
  bool isObject() const { return t_ == Object; }

  double             num()  const { if (t_ != Number) throw std::runtime_error("JSON: expected number"); return n_; }
  bool               boolean() const { if (t_ != Bool) throw std::runtime_error("JSON: expected bool"); return b_; }
  const std::string& str()  const { if (t_ != String) throw std::runtime_error("JSON: expected string"); return s_; }

  // Array access
  std::size_t size() const { return t_ == Array ? arr_.size() : obj_.size(); }
  const Value& operator[](std::size_t i) const { return arr_.at(i); }
  Value& push(const Value& v) { t_ = Array; arr_.push_back(v); return arr_.back(); }

  // Object access
  bool has(const std::string& k) const;
  const Value& get(const std::string& k) const;          // throws if missing
  Value& set(const std::string& k, const Value& v);      // insert or replace

  // Typed lookups with defaults (missing or null → default)
  double      numOr(const std::string& k, double d) const;
  bool        boolOr(const std::string& k, bool d) const;
  std::string strOr(const std::string& k, const std::string& d) const;

  std::string dump() const;

private:
  Type t_;
  bool b_ = false;
  double n_ = 0;
  std::string s_;
  std::vector<Value> arr_;
  std::vector<std::pair<std::string, Value>> obj_;

  void dumpTo(std::string& out) const;
};

Value parse(const std::string& text);

} // namespace json
