#include "core.hpp"
#include "valuation.hpp"
#include "factors.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <chrono>
using namespace pg;
void need(bool x,const char* s){if(!x)throw std::runtime_error(s);}
template<class F> void expect(F fn,const std::string& context){
 bool caught=false;
 try{fn();}catch(const std::exception& e){
  const std::string msg=e.what();
  need(msg.find(context)!=std::string::npos,"missing deal context");
  need(msg.find("rate out of range")!=std::string::npos,"missing underlying cause");
  caught=true;
 }
 need(caught,"partial calculation was accepted");
}
int main(){try{
 Config c;c.new_count=0;
 Deal good{7,10000000,12,0,0,1200};
 Deal bad=good;bad.id=42;bad.rate=-1;
 std::vector<Deal> deals{good,bad};
 expect([&]{value_portfolio(c,deals);},"Valuation deal 42");
 expect([&]{analyse_factors(c,deals);},"Factor analysis original deal 42");
 auto invalid=good;invalid.payment=3;
 bool rejected=false;try{value_portfolio(c,{invalid});}catch(const std::exception&){rejected=true;}
 need(rejected,"invalid payment enum accepted");
 Config nc=c;nc.new_count=1;nc.floating=10000;nc.min_spread=nc.max_spread=100000;
 expect([&]{analyse_factors(nc,{good});},"Factor analysis new deal 1");
 const auto dir=std::filesystem::temp_directory_path()/ ("strict-report-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
 need(!std::filesystem::exists(dir),"test directory occupied");
 expect([&]{report(c,deals,dir);},"Valuation deal 42");
 need(!std::filesystem::exists(dir),"output created before valuation succeeded");
 std::filesystem::create_directory(dir);
 try{
  expect([&]{report(nc,{good},dir);},"Factor analysis new deal 1");
  need(std::filesystem::is_empty(dir),"partial files left after factor failure");
 }catch(...){std::filesystem::remove(dir);throw;}
 std::filesystem::remove(dir);
 std::cout<<"Strict calculation tests passed\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
