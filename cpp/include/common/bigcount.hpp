/* =============================================================
   LOA Hybrid — BigCount: exact counting of huge search spaces
   Used only for sizes such as |DPG paths| and the brute-force
   space |S|^|V| × Π(arrangements). Latencies stay as double.

   Build with -DUSE_GMP (and -lgmpxx -lgmp) for exact GMP integers.
   Without GMP it falls back to a saturating 64-bit counter that
   prints ">= 18446744073709551615" when it overflows.
   ============================================================= */
#pragma once
#include <string>
#include <cstdint>
#include <cstdio>
#include <limits>

#ifdef USE_GMP
#include <gmpxx.h>

class BigCount {
public:
  BigCount(unsigned long v = 0) : v_(v) {}
  BigCount& operator+=(const BigCount& o) { v_ += o.v_; return *this; }
  BigCount& operator*=(const BigCount& o) { v_ *= o.v_; return *this; }
  friend BigCount operator+(BigCount a, const BigCount& b) { a += b; return a; }
  friend BigCount operator*(BigCount a, const BigCount& b) { a *= b; return a; }
  bool operator<=(double x) const { return mpz_cmp_d(v_.get_mpz_t(), x) <= 0; }
  bool isZero() const { return v_ == 0; }
  double toDouble() const { return v_.get_d(); }
  std::string str() const { return v_.get_str(); }
  static bool exact() { return true; }
  static const char* backend() { return "GMP"; }
private:
  mpz_class v_;
};

#else

class BigCount {
public:
  BigCount(std::uint64_t v = 0) : v_(v), sat_(false) {}
  BigCount& operator+=(const BigCount& o) {
    if (sat_ || o.sat_ || v_ > MAXV - o.v_) { v_ = MAXV; sat_ = true; }
    else v_ += o.v_;
    return *this;
  }
  BigCount& operator*=(const BigCount& o) {
    if (v_ == 0 || o.v_ == 0) { v_ = 0; sat_ = false; return *this; }
    if (sat_ || o.sat_ || v_ > MAXV / o.v_) { v_ = MAXV; sat_ = true; }
    else v_ *= o.v_;
    return *this;
  }
  friend BigCount operator+(BigCount a, const BigCount& b) { a += b; return a; }
  friend BigCount operator*(BigCount a, const BigCount& b) { a *= b; return a; }
  bool operator<=(double x) const { return !sat_ && static_cast<double>(v_) <= x; }
  bool isZero() const { return v_ == 0 && !sat_; }
  double toDouble() const { return static_cast<double>(v_); }
  std::string str() const {
    // Manual conversion: old MinGW printf does not understand %llu
    std::string digits;
    std::uint64_t x = v_;
    do { digits.insert(digits.begin(), static_cast<char>('0' + x % 10)); x /= 10; } while (x);
    return sat_ ? ">= " + digits : digits;
  }
  static bool exact() { return false; }
  static const char* backend() { return "uint64 (saturating)"; }
private:
  static constexpr std::uint64_t MAXV = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t v_;
  bool sat_;
};

#endif
