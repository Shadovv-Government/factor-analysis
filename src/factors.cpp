#include "factors.hpp"
#include "cashflow.hpp"
#include "valuation.hpp"
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace pg {
namespace {

std::string money(double rubles) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", rubles);
    return buffer;
}

std::string percent(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
    return buffer;
}

struct Item {
    const char* name;
    double value;
};

}

FactorAnalysis analyse_factors(const Config& c, const std::vector<Deal>& old_deals) {
    FactorAnalysis f;
    f.date0 = c.valuation_date;
    f.date1 = c.scenario_date;
    f.rate0_bps = c.discount_rate;
    f.rate1_bps = c.scenario_discount;
    f.new_deals = static_cast<unsigned>(c.new_count);
    const cf::Date dates[2] = {cf::parse_date(c.valuation_date), cf::parse_date(c.scenario_date)};
    const std::int64_t rates[2] = {static_cast<std::int64_t>(c.discount_rate) * 100000LL,
                                   static_cast<std::int64_t>(c.scenario_discount) * 100000LL};

    auto add = [&](const Deal& d, const cf::Date& origin, bool is_new) {
        try {
            const auto rows = cf::schedule(make_cf_deal(c, d, origin), cf_type(d.payment));
            for (int t = 0; t < 2; ++t) {
                for (int r = 0; r < 2; ++r) {
                    const auto p = cf::reprice(rows, dates[t], rates[r]);
                    const double received = static_cast<double>(p.received) / 100.0;
                    const double value = p.pv + received;
                    f.v[t][r][1] += value;
                    if (!is_new) f.v[t][r][0] += value;
                    if (!is_new && t == 1 && r == 0) f.received += received;
                }
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("Factor analysis ") + (is_new ? "new" : "original") + " deal " + std::to_string(d.id) + ": " + e.what());
        }
    };

    for (const auto& d : old_deals) add(d, dates[0], false);
    if (c.new_count > 0) {
        Config nc = c;
        nc.count = static_cast<unsigned>(c.new_count);
        nc.seed = c.new_seed;
        for (const auto& d : generate(nc)) add(d, dates[1], true);
    }

    const auto& v = f.v;
    f.time = v[1][0][0] - v[0][0][0];
    f.rate = v[0][1][0] - v[0][0][0];
    f.composition = v[0][0][1] - v[0][0][0];
    f.time_rate = v[1][1][0] - v[1][0][0] - v[0][1][0] + v[0][0][0];
    f.time_composition = v[1][0][1] - v[1][0][0] - v[0][0][1] + v[0][0][0];
    f.rate_composition = v[0][1][1] - v[0][1][0] - v[0][0][1] + v[0][0][0];
    f.triple = v[1][1][1] - v[1][1][0] - v[1][0][1] - v[0][1][1] + v[1][0][0] + v[0][1][0] + v[0][0][1] - v[0][0][0];
    f.total = v[1][1][1] - v[0][0][0];
    return f;
}

std::string factors_csv(const FactorAnalysis& f) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    const Item rows[] = {{"time", f.time},
                         {"rate", f.rate},
                         {"composition", f.composition},
                         {"time_x_rate", f.time_rate},
                         {"time_x_composition", f.time_composition},
                         {"rate_x_composition", f.rate_composition},
                         {"time_x_rate_x_composition", f.triple},
                         {"total_change", f.total},
                         {"value_before", f.v[0][0][0]},
                         {"value_after", f.v[1][1][1]},
                         {"cash_received_between_dates", f.received}};
    s << "factor,value_rub\n";
    for (const auto& r : rows) s << r.name << ',' << money(r.value) << '\n';
    return s.str();
}

std::string factors_html(const FactorAnalysis& f) {
    std::ostringstream h;
    h.imbue(std::locale::classic());
    h << "<section><h2>Факторный анализ изменения стоимости</h2><p>Сравниваются два состояния: " << f.date0 << ", ставка "
      << f.rate0_bps / 100.0 << "% и " << f.date1 << ", ставка " << f.rate1_bps / 100.0
      << "%; в сценарий добавлено новых сделок: " << f.new_deals << ".</p>"
      << "<p class='hint'>Стоимость = приведённая стоимость будущих платежей + деньги, уже полученные к дате оценки (без дисконтирования). "
         "Вклад фактора — изменение стоимости, когда меняется только он, относительно исходного состояния. "
         "Взаимодействия — поправки за одновременное изменение нескольких факторов. Сумма всех вкладов точно равна изменению стоимости. "
         "Это изменение приведённой стоимости на учебных допущениях, а не Economic Capital.</p>";
    h << "<p>Стоимость до: <b>" << money(f.v[0][0][0]) << " ₽</b> · после: <b>" << money(f.v[1][1][1])
      << " ₽</b> · изменение: <b>" << money(f.total) << " ₽</b>. Выплаты между датами, учтённые в стоимости: "
      << money(f.received) << " ₽.</p>";
    const Item items[] = {{"Течение времени", f.time},
                          {"Сдвиг ставки", f.rate},
                          {"Изменение состава", f.composition},
                          {"Время × ставка", f.time_rate},
                          {"Время × состав", f.time_composition},
                          {"Ставка × состав", f.rate_composition},
                          {"Время × ставка × состав", f.triple}};
    double max_abs = 1e-9, sum = 0;
    for (const auto& it : items) {
        max_abs = std::fmax(max_abs, std::fabs(it.value));
        sum += it.value;
    }
    h << "<table><thead><tr><th>Фактор</th><th>Вклад, ₽</th><th></th></tr></thead><tbody>";
    for (const auto& it : items) {
        h << "<tr><td>" << it.name << "</td><td>" << money(it.value) << "</td><td><div style='height:14px;width:"
          << percent(100.0 * std::fabs(it.value) / max_abs) << "%;background:" << (it.value < 0 ? "#bc475b" : "#4378dc")
          << "'></div></td></tr>";
    }
    h << "</tbody></table><p class='hint'>Контроль: сумма вкладов " << money(sum) << " ₽, расхождение с изменением стоимости "
      << money(sum - f.total) << " ₽. <a href='factor_analysis.csv' download>Вклады факторов (CSV)</a></p>";
    if (f.failed)
        h << "<p class='hint'>Не рассчитано сделок: " << f.failed << ". Первая ошибка: " << f.first_error << "</p>";
    h << "</section>";
    return h.str();
}

}
