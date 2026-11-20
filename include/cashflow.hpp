#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace cf {

struct Date {
    int y;
    unsigned m, d;
};
bool operator<(const Date& a, const Date& b);
bool operator==(const Date& a, const Date& b);
std::string to_iso(const Date& date);
Date parse_date(const std::string& iso);
std::int64_t parse_rate_nano(const std::string& decimal);

enum class Type { ANN, SIMPLE, RGM };

struct Deal {
    Date valuation{}, first_payment{}, maturity{};
    std::int64_t principal = 0;
    std::int64_t credit_rate_nano = 0;
    std::int64_t discount_rate_nano = 0;
    unsigned frequency_months = 1;
};

struct Payment {
    unsigned number = 0;
    Date date{};
    int days = 0;
    std::int64_t opening = 0, interest = 0, principal = 0, payment = 0, closing = 0;
    double discount_factor = 1, pv_interest = 0, pv_principal = 0, pv_payment = 0;
};

struct Summary {
    unsigned count = 0;
    std::int64_t total_interest = 0, total_principal = 0, total_payments = 0;
    std::int64_t first_payment = 0, last_payment = 0, closing_balance = 0;
    double pv_interest = 0, pv_principal = 0, pv_future = 0, pv_minus_outstanding = 0;
};

void validate(const Deal& deal);
std::vector<Date> payment_dates(const Deal& deal);
std::vector<Payment> schedule(const Deal& deal, Type type);
struct Repriced {
    double pv = 0;
    std::int64_t received = 0;
};
Repriced reprice(const std::vector<Payment>& rows, const Date& pv_date, std::int64_t discount_rate_nano);
Summary summarize(const std::vector<Payment>& rows);

}
