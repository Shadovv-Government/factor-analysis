#include "cashflow.hpp"
#include "core.hpp"
#include "factors.hpp"
#include "valuation.hpp"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void check(bool ok, const std::string& what) {
    if (!ok) throw std::runtime_error(what);
}

double effects_sum(const pg::FactorAnalysis& f) {
    return f.time + f.rate + f.composition + f.time_rate + f.time_composition + f.rate_composition + f.triple;
}

void identity() {
    pg::Config c;
    const auto deals = pg::generate(c);
    const auto f = pg::analyse_factors(c, deals);
    check(f.failed == 0, "no failures: " + f.first_error);
    check(std::fabs(effects_sum(f) - f.total) <= 1e-3, "effects must add up to the total change");
    const auto v = pg::value_portfolio(c, deals);
    check(std::fabs(f.v[0][0][0] - v.total.pv) <= 1e-2, "base value must equal portfolio PV");
    check(f.received > 0, "cash is received between the dates");
}

void unchanged_rate_and_composition() {
    pg::Config c;
    const auto deals = pg::generate(c);
    c.scenario_discount = c.discount_rate;
    c.new_count = 0;
    const auto f = pg::analyse_factors(c, deals);
    check(std::fabs(f.rate) <= 1e-3 && std::fabs(f.composition) <= 1e-3, "no rate or composition effect");
    check(std::fabs(f.time_rate) <= 1e-3 && std::fabs(f.time_composition) <= 1e-3 && std::fabs(f.rate_composition) <= 1e-3 &&
              std::fabs(f.triple) <= 1e-3,
          "no interaction effects");
    check(std::fabs(f.time - f.total) <= 1e-3, "time explains everything");
}

void rate_only() {
    pg::Config c;
    const auto deals = pg::generate(c);
    c.scenario_date = c.valuation_date;
    c.new_count = 0;
    c.scenario_discount = 1500;
    const auto f = pg::analyse_factors(c, deals);
    check(std::fabs(f.time) <= 1e-3, "no time effect on the same date");
    check(f.rate < 0, "higher discount rate lowers value");
    check(std::fabs(f.rate - f.total) <= 1e-3, "rate explains everything");
}

void composition_only() {
    pg::Config c;
    c.new_count = 100;
    const auto f = pg::analyse_factors(c, pg::generate(c));
    check(f.failed == 0, "no failures: " + f.first_error);
    check(f.composition > 0, "new deals add value");
    check(f.v[1][1][1] > f.v[1][1][0], "new deals add value in the scenario state");
    check(std::fabs(effects_sum(f) - f.total) <= 1e-3, "effects add up with new deals");
}

void reprice_checks() {
    cf::Deal d;
    d.valuation = cf::parse_date("2022-08-31");
    d.first_payment = cf::parse_date("2022-09-22");
    d.maturity = cf::parse_date("2050-06-15");
    d.principal = 364603789;
    d.credit_rate_nano = cf::parse_rate_nano("0.075");
    d.discount_rate_nano = cf::parse_rate_nano("0.075");
    const auto rows = cf::schedule(d, cf::Type::ANN);
    const auto s = cf::summarize(rows);
    const auto full = cf::reprice(rows, d.valuation, d.discount_rate_nano);
    check(std::fabs(full.pv - s.pv_future) <= 1e-6 && full.received == 0, "reprice at valuation date equals schedule PV");
    const auto part = cf::reprice(rows, rows[9].date, d.discount_rate_nano);
    std::int64_t paid = 0;
    for (int i = 0; i < 10; ++i) paid += rows[i].payment;
    check(part.received == paid, "received cash is the sum of earlier payments");
    check(std::fabs(part.pv - static_cast<double>(rows[9].closing) / 100.0) <= 0.5, "PV at a payment date equals the balance");
}

void config_keys() {
    namespace fs = std::filesystem;
    const auto path = fs::temp_directory_path() / "portfolio-factors-config.ini";
    fs::remove(path);
    pg::Config c;
    c.scenario_date = "2023-01-31";
    c.scenario_discount = 900;
    c.new_count = 7;
    c.new_seed = 99;
    pg::save(c, path);
    const auto back = pg::load(path);
    check(back.scenario_date == "2023-01-31" && back.scenario_discount == 900 && back.new_count == 7 && back.new_seed == 99,
          "scenario keys round trip");
    fs::remove(path);
    pg::Config bad;
    bad.scenario_date = bad.valuation_date;
    bool rejected = false;
    try { pg::validate(bad); } catch (const std::exception&) { rejected = true; }
    check(rejected, "scenario date must be after valuation date");
}

}

int main() {
    try {
        reprice_checks();
        identity();
        unchanged_rate_and_composition();
        rate_only();
        composition_only();
        config_keys();
        std::cout << "factor tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
