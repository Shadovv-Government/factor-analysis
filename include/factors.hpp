#pragma once
#include "core.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace pg {

struct FactorAnalysis {
    std::string date0, date1;
    int rate0_bps = 0, rate1_bps = 0;
    unsigned new_deals = 0;
    double v[2][2][2] = {};
    double received = 0;
    double time = 0, rate = 0, composition = 0;
    double time_rate = 0, time_composition = 0, rate_composition = 0, triple = 0;
    double total = 0;
    std::size_t failed = 0;
    std::string first_error;
};

FactorAnalysis analyse_factors(const Config& config, const std::vector<Deal>& deals);
std::string factors_csv(const FactorAnalysis& analysis);
std::string factors_html(const FactorAnalysis& analysis);

}
