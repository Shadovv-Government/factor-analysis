#include "cashflows.hpp"
#include <type_traits>

static_assert(sizeof(cashflows::MoneyMinor) == 8);
static_assert(std::is_same_v<decltype(cashflows::build_schedule(
    std::declval<const cashflows::Deal&>(), cashflows::PaymentType::ANN)),
    std::vector<cashflows::Payment>>);
static_assert(std::is_same_v<decltype(cashflows::discount(
    std::declval<const cashflows::Deal&>(),
    std::declval<const cashflows::DiscountSpec&>(),
    std::declval<const std::vector<cashflows::Payment>&>())),
    std::vector<cashflows::ValuedPayment>>);
