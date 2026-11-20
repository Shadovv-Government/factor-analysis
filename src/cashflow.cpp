#include "cashflow.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace cf {
namespace {

constexpr std::int64_t kMaxPrincipal = 9000000000000000LL;
constexpr std::int64_t kMaxRateNano = 10000000000LL;
constexpr std::uint64_t kYearNano = 365ULL * 1000000000ULL;
constexpr unsigned kMaxPayments = 1200;

struct DD {
    double hi = 0, lo = 0;
};
DD quick_two_sum(double a, double b) { double s = a + b; return {s, b - (s - a)}; }
DD two_sum(double a, double b) { double s = a + b, v = s - a; return {s, (a - (s - v)) + (b - v)}; }
DD two_prod(double a, double b) { double p = a * b; return {p, std::fma(a, b, -p)}; }
DD dadd(DD a, DD b) {
    DD s = two_sum(a.hi, b.hi), t = two_sum(a.lo, b.lo);
    s.lo += t.hi;
    s = quick_two_sum(s.hi, s.lo);
    s.lo += t.lo;
    return quick_two_sum(s.hi, s.lo);
}
DD dneg(DD a) { return {-a.hi, -a.lo}; }
DD dmul_d(DD a, double b) { DD p = two_prod(a.hi, b); p.lo += a.lo * b; return quick_two_sum(p.hi, p.lo); }
DD dmul(DD a, DD b) { DD p = two_prod(a.hi, b.hi); p.lo += a.hi * b.lo + a.lo * b.hi; return quick_two_sum(p.hi, p.lo); }
DD ddiv(DD a, DD b) {
    const double q1 = a.hi / b.hi;
    DD r = dadd(a, dneg(dmul_d(b, q1)));
    const double q2 = r.hi / b.hi;
    r = dadd(r, dneg(dmul_d(b, q2)));
    const double q3 = r.hi / b.hi;
    return dadd(quick_two_sum(q1, q2), DD{q3, 0});
}
double to_double(DD x) { return x.hi + x.lo; }

std::int64_t round_half_up(DD x) {
    double fl = std::floor(x.hi), frac = (x.hi - fl) + x.lo;
    while (frac < 0) { fl -= 1; frac += 1; }
    while (frac >= 1) { fl += 1; frac -= 1; }
    return static_cast<std::int64_t>(fl) + (frac >= 0.5 ? 1 : 0);
}

struct U128 {
    std::uint64_t hi, lo;
};
U128 mul64(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t mask = 0xFFFFFFFFULL;
    const std::uint64_t a0 = a & mask, a1 = a >> 32, b0 = b & mask, b1 = b >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t mid = (p00 >> 32) + (p01 & mask) + (p10 & mask);
    return {p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32), (p00 & mask) | (mid << 32)};
}
U128 mul128_64(U128 a, std::uint64_t b) {
    const U128 low = mul64(a.lo, b), high = mul64(a.hi, b);
    if (high.hi != 0) throw std::overflow_error("interest overflow");
    U128 r{low.hi + high.lo, low.lo};
    if (r.hi < low.hi) throw std::overflow_error("interest overflow");
    return r;
}
std::int64_t round_div(U128 n, std::uint64_t d) {
    if (n.hi == 0 && n.lo < (1ULL << 62)) return static_cast<std::int64_t>((2 * n.lo + d) / (2 * d));
    if (n.hi >> 62) throw std::overflow_error("interest overflow");
    U128 x{(n.hi << 1) | (n.lo >> 63), n.lo << 1};
    x.lo += d;
    if (x.lo < d) ++x.hi;
    const std::uint64_t den = 2 * d;
    std::uint64_t rem = 0, qhi = 0, qlo = 0;
    for (int bit = 127; bit >= 0; --bit) {
        const std::uint64_t b = bit >= 64 ? (x.hi >> (bit - 64)) & 1 : (x.lo >> bit) & 1;
        rem = (rem << 1) | b;
        qhi = (qhi << 1) | (qlo >> 63);
        qlo <<= 1;
        if (rem >= den) { rem -= den; qlo |= 1; }
    }
    if (qhi != 0 || qlo > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        throw std::overflow_error("interest overflow");
    return static_cast<std::int64_t>(qlo);
}
std::int64_t interest_kopecks(std::int64_t balance, std::int64_t rate_nano, int days) {
    if (balance == 0 || rate_nano == 0 || days == 0) return 0;
    const U128 n = mul128_64(mul64(static_cast<std::uint64_t>(balance), static_cast<std::uint64_t>(rate_nano)),
                             static_cast<std::uint64_t>(days));
    return round_div(n, kYearNano);
}

bool leap(int y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
unsigned days_in_month(int y, unsigned m) {
    static const unsigned table[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && leap(y)) ? 29 : table[m - 1];
}
bool valid(const Date& d) {
    return d.y >= 1 && d.y <= 9999 && d.m >= 1 && d.m <= 12 && d.d >= 1 && d.d <= days_in_month(d.y, d.m);
}
long long serial(const Date& date) {
    const int y = date.y - (date.m <= 2 ? 1 : 0);
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const int mp = static_cast<int>(date.m) + (date.m > 2 ? -3 : 9);
    const unsigned doy = static_cast<unsigned>((153 * mp + 2) / 5) + date.d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

DD year_nano() { return DD{365e9, 0}; }
DD period_rate(std::int64_t rate_nano, int days) {
    return ddiv(two_prod(static_cast<double>(rate_nano), static_cast<double>(days)), year_nano());
}

}

bool operator<(const Date& a, const Date& b) {
    return a.y != b.y ? a.y < b.y : (a.m != b.m ? a.m < b.m : a.d < b.d);
}
bool operator==(const Date& a, const Date& b) { return a.y == b.y && a.m == b.m && a.d == b.d; }

std::string to_iso(const Date& date) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", date.y, date.m, date.d);
    return buffer;
}

Date parse_date(const std::string& iso) {
    auto digit = [&](std::size_t i) {
        if (iso[i] < '0' || iso[i] > '9') throw std::invalid_argument("date: expected YYYY-MM-DD");
        return iso[i] - '0';
    };
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') throw std::invalid_argument("date: expected YYYY-MM-DD");
    Date date{digit(0) * 1000 + digit(1) * 100 + digit(2) * 10 + digit(3),
              static_cast<unsigned>(digit(5) * 10 + digit(6)), static_cast<unsigned>(digit(8) * 10 + digit(9))};
    if (!valid(date)) throw std::invalid_argument("date: invalid calendar date");
    return date;
}

std::int64_t parse_rate_nano(const std::string& text) {
    if (text.empty() || text.size() > 24 || text[0] < '0' || text[0] > '9') throw std::invalid_argument("rate: expected nonnegative decimal");
    std::int64_t whole = 0, fraction = 0;
    int digits = 0;
    bool dot = false;
    for (char c : text) {
        if (c == '.') {
            if (dot) throw std::invalid_argument("rate: expected nonnegative decimal");
            dot = true;
        } else if (c < '0' || c > '9') {
            throw std::invalid_argument("rate: expected nonnegative decimal");
        } else if (!dot) {
            whole = whole * 10 + (c - '0');
            if (whole > 10) throw std::invalid_argument("rate: too large");
        } else {
            if (digits == 9) throw std::invalid_argument("rate: at most 9 decimal places");
            fraction = fraction * 10 + (c - '0');
            ++digits;
        }
    }
    if (dot && digits == 0) throw std::invalid_argument("rate: expected nonnegative decimal");
    while (digits < 9) { fraction *= 10; ++digits; }
    const std::int64_t nano = whole * 1000000000LL + fraction;
    if (nano > kMaxRateNano) throw std::invalid_argument("rate: too large");
    return nano;
}

void validate(const Deal& d) {
    if (!valid(d.valuation) || !valid(d.first_payment) || !valid(d.maturity)) throw std::invalid_argument("invalid date");
    if (!(d.valuation < d.first_payment) || d.maturity < d.first_payment)
        throw std::invalid_argument("expected valuation < first payment <= maturity");
    if (d.frequency_months < 1 || d.frequency_months > 120) throw std::invalid_argument("frequency_months: 1..120");
    if (d.principal <= 0 || d.principal > kMaxPrincipal) throw std::invalid_argument("principal out of range");
    if (d.credit_rate_nano < 0 || d.credit_rate_nano > kMaxRateNano || d.discount_rate_nano < 0 || d.discount_rate_nano > kMaxRateNano)
        throw std::invalid_argument("rate out of range");
}

std::vector<Date> payment_dates(const Deal& d) {
    validate(d);
    std::vector<Date> out;
    const long long maturity_month = static_cast<long long>(d.maturity.y) * 12 + d.maturity.m - 1;
    for (unsigned i = 0; i < kMaxPayments; ++i) {
        const long long absolute = static_cast<long long>(d.first_payment.y) * 12 + d.first_payment.m - 1 +
                                   static_cast<long long>(i) * d.frequency_months;
        if (absolute > maturity_month) { out.push_back(d.maturity); return out; }
        const int year = static_cast<int>(absolute / 12);
        const unsigned month = static_cast<unsigned>(absolute % 12) + 1;
        const Date candidate{year, month, std::min(d.first_payment.d, days_in_month(year, month))};
        if (!(candidate < d.maturity)) { out.push_back(d.maturity); return out; }
        out.push_back(candidate);
    }
    throw std::invalid_argument("at most 1200 payments are supported");
}

std::vector<Payment> schedule(const Deal& deal, Type type) {
    const auto dates = payment_dates(deal);
    const std::size_t n = dates.size();
    std::vector<int> days(n);
    long long previous = serial(deal.valuation);
    for (std::size_t i = 0; i < n; ++i) {
        const long long current = serial(dates[i]);
        days[i] = static_cast<int>(current - previous);
        previous = current;
    }
    const DD one{1, 0};
    DD credit_df = one, sum_df{0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        credit_df = ddiv(credit_df, dadd(one, period_rate(deal.credit_rate_nano, days[i])));
        sum_df = dadd(sum_df, credit_df);
    }
    const std::int64_t regular = round_half_up(ddiv(DD{static_cast<double>(deal.principal), 0}, sum_df));
    const std::int64_t count = static_cast<std::int64_t>(n);
    const std::int64_t regular_principal = (2 * deal.principal + count) / (2 * count);

    std::vector<Payment> rows;
    rows.reserve(n);
    DD discount_df = one;
    std::int64_t balance = deal.principal;
    for (std::size_t i = 0; i < n; ++i) {
        Payment p;
        p.number = static_cast<unsigned>(i + 1);
        p.date = dates[i];
        p.days = days[i];
        p.opening = balance;
        p.interest = interest_kopecks(balance, deal.credit_rate_nano, days[i]);
        if (i + 1 == n) {
            p.principal = balance;
        } else if (type == Type::SIMPLE) {
            p.principal = 0;
        } else if (type == Type::RGM) {
            p.principal = std::min(balance, regular_principal);
        } else {
            p.principal = std::min(regular, balance + p.interest) - p.interest;
            if (p.principal < 0) throw std::invalid_argument("ANN payment is below interest: negative amortization is not supported");
        }
        p.payment = p.interest + p.principal;
        p.closing = balance - p.principal;
        discount_df = ddiv(discount_df, dadd(one, period_rate(deal.discount_rate_nano, days[i])));
        const DD hundred{100, 0};
        auto pv = [&](std::int64_t kopecks) {
            return to_double(ddiv(dmul(DD{static_cast<double>(kopecks), 0}, discount_df), hundred));
        };
        p.discount_factor = to_double(discount_df);
        p.pv_interest = pv(p.interest);
        p.pv_principal = pv(p.principal);
        p.pv_payment = pv(p.payment);
        rows.push_back(p);
        balance = p.closing;
    }
    return rows;
}

Repriced reprice(const std::vector<Payment>& rows, const Date& pv_date, std::int64_t rate_nano) {
    if (!valid(pv_date)) throw std::invalid_argument("invalid pv date");
    if (rate_nano < 0 || rate_nano > kMaxRateNano) throw std::invalid_argument("rate out of range");
    Repriced out;
    const DD one{1, 0}, hundred{100, 0};
    DD df = one, total{0, 0};
    const long long pv_serial = serial(pv_date);
    long long previous = pv_serial;
    for (const auto& r : rows) {
        const long long current = serial(r.date);
        if (current <= pv_serial) {
            out.received += r.payment;
            continue;
        }
        df = ddiv(df, dadd(one, period_rate(rate_nano, static_cast<int>(current - previous))));
        total = dadd(total, ddiv(dmul(DD{static_cast<double>(r.payment), 0}, df), hundred));
        previous = current;
    }
    out.pv = to_double(total);
    return out;
}

Summary summarize(const std::vector<Payment>& rows) {
    if (rows.empty()) throw std::invalid_argument("empty schedule");
    Summary s;
    s.count = static_cast<unsigned>(rows.size());
    for (const auto& r : rows) {
        s.total_interest += r.interest;
        s.total_principal += r.principal;
        s.total_payments += r.payment;
        s.pv_interest += r.pv_interest;
        s.pv_principal += r.pv_principal;
        s.pv_future += r.pv_payment;
    }
    s.first_payment = rows.front().payment;
    s.last_payment = rows.back().payment;
    s.closing_balance = rows.back().closing;
    s.pv_minus_outstanding = s.pv_future - static_cast<double>(rows.front().opening) / 100.0;
    return s;
}

}
