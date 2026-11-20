#include "valuation.hpp"
#include "cashflow.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace pg {
namespace {

unsigned month_length(int year, unsigned month) {
    static const unsigned table[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    return (month == 2 && leap) ? 29 : table[month - 1];
}

cf::Date add_months(const cf::Date& date, int months) {
    const long long total = static_cast<long long>(date.y) * 12 + (date.m - 1) + months;
    const int year = static_cast<int>(total / 12);
    const unsigned month = static_cast<unsigned>(total % 12) + 1;
    return {year, month, std::min(date.d, month_length(year, month))};
}

std::string rub(std::int64_t kopecks) {
    const long long a = std::llabs(static_cast<long long>(kopecks));
    char buffer[48];
    std::snprintf(buffer, sizeof(buffer), "%s%lld.%02lld", kopecks < 0 ? "-" : "", a / 100, a % 100);
    return buffer;
}

std::string money(double rubles) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.2f", rubles);
    return buffer;
}

std::string escape(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (c == '<') out += "&lt;";
        else if (c == '>') out += "&gt;";
        else if (c == '&') out += "&amp;";
        else out += c;
    }
    return out;
}

}

cf::Deal make_cf_deal(const Config& c, const Deal& d, const cf::Date& origin) {
    if (d.payment < 0 || d.payment > 2) throw std::invalid_argument("invalid payment type");
    if (d.floating != 0 && d.floating != 1) throw std::invalid_argument("invalid rate type");
    if (d.term < 1 || d.term > 1200) throw std::invalid_argument("invalid term");
    cf::Deal x;
    x.valuation = origin;
    x.first_payment = add_months(origin, 1);
    x.maturity = add_months(origin, d.term);
    x.principal = d.amount;
    const std::int64_t bps = d.floating ? static_cast<std::int64_t>(c.index_rate) + d.rate : d.rate;
    x.credit_rate_nano = bps * 100000LL;
    x.discount_rate_nano = static_cast<std::int64_t>(c.discount_rate) * 100000LL;
    x.frequency_months = 1;
    return x;
}

cf::Type cf_type(int payment) {
    return payment == 0 ? cf::Type::ANN : (payment == 1 ? cf::Type::SIMPLE : cf::Type::RGM);
}

Valuation value_portfolio(const Config& c, const std::vector<Deal>& deals) {
    Valuation v;
    v.date = c.valuation_date;
    v.discount_bps = c.discount_rate;
    v.index_bps = c.index_rate;
    const cf::Date valuation = cf::parse_date(c.valuation_date);
    for (const auto& d : deals) {
        try {
            const cf::Deal x = make_cf_deal(c, d, valuation);
            const cf::Type type = cf_type(d.payment);
            const auto rows = cf::schedule(x, type);
            auto& totals = v.by_type[static_cast<std::size_t>(d.payment)];
            ++totals.deals;
            totals.principal += d.amount;
            for (const auto& r : rows) {
                totals.interest += r.interest;
                totals.payments += r.payment;
                totals.pv += r.pv_payment;
                char key[16];
                std::snprintf(key, sizeof(key), "%04d-%02u", r.date.y, r.date.m);
                auto& m = v.months[key];
                m.interest += r.interest;
                m.principal += r.principal;
                m.payment += r.payment;
                m.pv += r.pv_payment;
            }
        } catch (const std::exception& e) {
            throw std::runtime_error("Valuation deal " + std::to_string(d.id) + ": " + e.what());
        }
    }
    for (const auto& t : v.by_type) {
        v.total.deals += t.deals;
        v.total.principal += t.principal;
        v.total.interest += t.interest;
        v.total.payments += t.payments;
        v.total.pv += t.pv;
    }
    return v;
}

std::string cashflows_csv(const Valuation& v) {
    std::ostringstream s;
    s.imbue(std::locale::classic());
    s << "month,interest_kopecks,principal_kopecks,payment_kopecks,pv_payment_rub\n";
    for (const auto& item : v.months)
        s << item.first << ',' << item.second.interest << ',' << item.second.principal << ',' << item.second.payment << ','
          << money(item.second.pv) << '\n';
    return s.str();
}

std::string valuation_html(const Valuation& v) {
    static const char* names[] = {"ANN", "SIMPLE", "RGM"};
    std::ostringstream h;
    h.imbue(std::locale::classic());
    h << "<section><h2>Оценка портфеля</h2><p>Дата оценки: " << escape(v.date) << " · ставка дисконтирования: "
      << v.discount_bps / 100.0 << "% · TEST_INDEX: " << v.index_bps / 100.0 << "% (плоский)</p>"
      << "<p class='hint'>Допущения: все сделки выданы на дату оценки, платежи ежемесячные, первый через месяц, ACT/365F, "
         "дисконтирование PERIOD_SIMPLE_CHAIN. Ставка дисконтирования и TEST_INDEX — учебные допущения. "
         "Это приведённая стоимость будущих платежей, а не Economic Capital.</p>"
      << "<table><thead><tr><th>Тип</th><th>Сделок</th><th>Долг, ₽</th><th>Проценты, ₽</th><th>Всего платежей, ₽</th>"
         "<th>PV, ₽</th><th>PV − долг, ₽</th></tr></thead><tbody>";
    auto row = [&](const std::string& name, const TypeTotals& t) {
        h << "<tr><td>" << name << "</td><td>" << t.deals << "</td><td>" << rub(t.principal) << "</td><td>" << rub(t.interest)
          << "</td><td>" << rub(t.payments) << "</td><td>" << money(t.pv) << "</td><td>"
          << money(t.pv - static_cast<double>(t.principal) / 100.0) << "</td></tr>";
    };
    for (std::size_t i = 0; i < 3; ++i) row(names[i], v.by_type[i]);
    row("Итого", v.total);
    h << "</tbody></table>";
    if (v.failed)
        h << "<p class='hint'>Не рассчитано сделок: " << v.failed << ". Первая ошибка: " << escape(v.first_error) << "</p>";
    h << "<h3>Платежи портфеля по месяцам</h3><table><thead><tr><th>Месяц</th><th>Проценты, ₽</th><th>Тело, ₽</th>"
         "<th>Платёж, ₽</th><th>PV, ₽</th></tr></thead><tbody>";
    int shown = 0;
    for (const auto& item : v.months) {
        if (++shown > 24) break;
        h << "<tr><td>" << item.first << "</td><td>" << rub(item.second.interest) << "</td><td>" << rub(item.second.principal)
          << "</td><td>" << rub(item.second.payment) << "</td><td>" << money(item.second.pv) << "</td></tr>";
    }
    h << "</tbody></table><p class='hint'>Показаны первые 24 месяца из " << v.months.size()
      << ". <a href='portfolio_cashflows.csv' download>Все платежи по месяцам (CSV)</a></p></section>";
    return h.str();
}

}
