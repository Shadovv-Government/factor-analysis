#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Interface proposal only. The implemented reference engine is Python/Decimal.
namespace cashflows {
using MoneyMinor = std::int64_t; // RUB kopecks; checked arithmetic required.
using DecimalString = std::string; // Exact decimal interchange, never binary money.
struct Date { int year; unsigned month; unsigned day; };
enum class PaymentType { ANN, SIMPLE, RGM };
enum class DayCount { Act365Fixed };
enum class DiscountMethod { PeriodSimpleChain };

struct Deal {
    std::string id;
    std::string currency;
    Date valuation_date;
    MoneyMinor outstanding_principal;
    DecimalString annual_credit_rate;
    Date first_payment_date;
    Date maturity_date;
    unsigned frequency_months;
    DayCount day_count;
};
struct DiscountSpec {
    DecimalString annual_rate;
    DayCount day_count;
    DiscountMethod method;
};
struct Payment {
    unsigned number;
    Date date;
    unsigned days;
    MoneyMinor opening_balance, interest, principal, payment, closing_balance;
    DecimalString credit_period_rate;
};
struct ValuedPayment {
    Payment cashflow;
    DecimalString discount_period_rate, discount_factor;
    DecimalString pv_interest, pv_principal, pv_payment;
};
struct Summary {
    unsigned payment_count;
    MoneyMinor total_interest, total_principal, total_payments;
    DecimalString pv_interest, pv_principal, pv_future_cashflows, pv_minus_outstanding;
    MoneyMinor first_payment, last_payment, closing_balance;
};
struct Diagnostic {
    std::string code, field, message;
};
struct CheckResult {
    bool passed;
    MoneyMinor principal_difference, closing_balance;
    std::vector<Diagnostic> failures;
};

// validate returns diagnostics. Calculation functions throw std::invalid_argument
// for an invalid request and std::overflow_error for unrepresentable arithmetic.
std::vector<Diagnostic> validate(const Deal&, const DiscountSpec&);
std::vector<Date> build_payment_dates(const Deal&);
std::vector<Payment> build_schedule(const Deal&, PaymentType);
std::vector<ValuedPayment> discount(const Deal&, const DiscountSpec&, const std::vector<Payment>&);
Summary summarize(const Deal&, const std::vector<ValuedPayment>&);
CheckResult check(const Deal&, const std::vector<ValuedPayment>&);
} // namespace cashflows
