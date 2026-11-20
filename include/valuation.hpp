#pragma once
#include "cashflow.hpp"
#include "core.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace pg {

struct TypeTotals {
    std::size_t deals = 0;
    std::int64_t principal = 0, interest = 0, payments = 0;
    double pv = 0;
};

struct MonthTotals {
    std::int64_t interest = 0, principal = 0, payment = 0;
    double pv = 0;
};

struct Valuation {
    std::string date;
    int discount_bps = 0, index_bps = 0;
    std::array<TypeTotals, 3> by_type{};
    TypeTotals total{};
    std::map<std::string, MonthTotals> months;
    std::size_t failed = 0;
    std::string first_error;
};

cf::Deal make_cf_deal(const Config& config, const Deal& deal, const cf::Date& origin);
cf::Type cf_type(int payment);
Valuation value_portfolio(const Config& config, const std::vector<Deal>& deals);
std::string cashflows_csv(const Valuation& valuation);
std::string valuation_html(const Valuation& valuation);

}
