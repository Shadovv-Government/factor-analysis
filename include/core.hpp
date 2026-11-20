#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace pg {
struct Config {
 unsigned count=1000,seed=42;
 std::int64_t min_amount=10000000,max_amount=500000000;
 int min_term=6,max_term=60;
 std::array<int,3> shares{6000,2500,1500};
 int floating=3000,min_rate=1200,max_rate=2400,min_spread=100,max_spread=500;
 std::string valuation_date="2022-08-31";
 int discount_rate=750,index_rate=750;
 std::string scenario_date="2022-11-30";
 int scenario_discount=850,new_count=100;
 unsigned new_seed=43;
};
struct Deal {unsigned id;std::int64_t amount;int term,payment,floating,rate;bool operator==(const Deal&)const;};
void validate(const Config&);
Config profile(const std::string&);
Config load(const std::filesystem::path&);
void save(const Config&,const std::filesystem::path&);
std::vector<Deal> generate(const Config&);
std::array<std::size_t,3> quotas(std::size_t,const std::array<int,3>&);
std::string summary(const Config&,const std::vector<Deal>&);
void report(const Config&,const std::vector<Deal>&,const std::filesystem::path&);
}
