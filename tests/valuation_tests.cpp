#include "core.hpp"
#include "cashflow.hpp"
#include "valuation.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error(what);
}

void wiring() {
    pg::Config c;
    c.count = 1;
    c.floating = 0;
    c.min_term = c.max_term = 6;
    c.discount_rate = 800;
    const auto deals = pg::generate(c);
    const auto v = pg::value_portfolio(c, deals);
    check(v.failed == 0, "no failures: " + v.first_error);
    const auto& d = deals[0];
    cf::Deal x;
    x.valuation = cf::parse_date("2022-08-31");
    x.first_payment = cf::parse_date("2022-09-30");
    x.maturity = cf::parse_date("2023-02-28");
    x.principal = d.amount;
    x.credit_rate_nano = static_cast<std::int64_t>(d.rate) * 100000;
    x.discount_rate_nano = 800LL * 100000;
    const cf::Type type = d.payment == 0 ? cf::Type::ANN : (d.payment == 1 ? cf::Type::SIMPLE : cf::Type::RGM);
    const auto s = cf::summarize(cf::schedule(x, type));
    check(v.total.payments == s.total_payments, "total payments");
    check(v.total.interest == s.total_interest, "total interest");
    check(v.total.principal == d.amount, "principal");
    check(std::fabs(v.total.pv - s.pv_future) <= 1e-6, "pv");
    check(v.months.size() == 6, "six monthly buckets");
}

void consistency() {
    pg::Config c;
    const auto deals = pg::generate(c);
    const auto v = pg::value_portfolio(c, deals);
    check(v.failed == 0, "no failures: " + v.first_error);
    std::size_t n = 0;
    std::int64_t principal = 0;
    for (const auto& t : v.by_type) n += t.deals;
    for (const auto& d : deals) principal += d.amount;
    check(n == deals.size(), "deal count");
    check(v.total.principal == principal, "total principal");
    check(v.total.payments == v.total.principal + v.total.interest, "payments = principal + interest");
    double pv = 0;
    std::int64_t month_payments = 0;
    for (const auto& item : v.months) {
        month_payments += item.second.payment;
        pv += item.second.pv;
    }
    check(month_payments == v.total.payments, "monthly payments add up");
    check(std::fabs(pv - v.total.pv) <= 1e-3, "monthly PV adds up");
    const auto again = pg::value_portfolio(c, deals);
    check(again.total.payments == v.total.payments && again.total.pv == v.total.pv, "deterministic");
    pg::Config high = c;
    high.discount_rate = 1500;
    const auto high_v = pg::value_portfolio(high, deals);
    check(high_v.total.pv < v.total.pv, "higher discount rate lowers PV");
    check(high_v.total.payments == v.total.payments, "flows do not depend on discount rate");
    pg::Config floating = c;
    floating.floating = 10000;
    const auto fdeals = pg::generate(floating);
    pg::Config indexed = floating;
    indexed.index_rate = 1500;
    check(pg::value_portfolio(indexed, fdeals).total.interest > pg::value_portfolio(floating, fdeals).total.interest,
          "higher index raises floating interest");
}

void config_io() {
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() / "portfolio-valuation-config.ini";
    fs::remove(path);
    pg::Config c;
    c.valuation_date = "2023-12-31";
    c.discount_rate = 900;
    c.index_rate = 650;
    c.scenario_date = "2024-03-31";
    pg::save(c, path);
    const auto back = pg::load(path);
    check(back.valuation_date == "2023-12-31" && back.discount_rate == 900 && back.index_rate == 650, "round trip");
    fs::remove(path);
    { std::ofstream f(path, std::ios::binary); f << "version=1.0.0\ncount=10\n"; }
    const auto old = pg::load(path);
    check(old.valuation_date == "2022-08-31" && old.discount_rate == 750 && old.index_rate == 750, "defaults for old configs");
    { std::ofstream f(path, std::ios::binary); f << "valuation_date=2022-02-30\n"; }
    bool reported = false;
    try { pg::load(path); }
    catch (const std::exception& e) { reported = std::string(e.what()).find("valuation_date") != std::string::npos; }
    check(reported, "bad valuation date must be reported by key");
    fs::remove(path);
}

}

int main() {
    try {
        wiring();
        consistency();
        config_io();
        std::cout << "valuation tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
