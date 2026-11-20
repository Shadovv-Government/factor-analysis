#include "core.hpp"
#include "cashflow.hpp"
#include "valuation.hpp"
#include "factors.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <locale>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <tuple>
namespace pg {
namespace {
const char* names[]={"ANN","SIMPLE","RGM"};
std::ofstream out(const std::filesystem::path& p){std::ofstream f(p,std::ios::binary);f.imbue(std::locale::classic());if(!f)throw std::runtime_error("Cannot write file");f.exceptions(std::ios::failbit|std::ios::badbit);return f;}
struct Random {
 std::mt19937 e;
 explicit Random(unsigned seed):e(seed){}
 std::uint64_t next(){auto a=std::uint64_t(e());auto b=std::uint64_t(e());return (a<<32)|b;}
 std::uint64_t bound(std::uint64_t n){auto threshold=(std::uint64_t(0)-n)%n;for(;;){auto x=next();if(x>=threshold)return x%n;}}
 std::int64_t range(std::int64_t a,std::int64_t b){return a+static_cast<std::int64_t>(bound(static_cast<std::uint64_t>(b-a)+1));}
 void shuffle(std::vector<int>& a){for(auto i=a.size();i>1;--i)std::swap(a[i-1],a[bound(i)]);}
};
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r\n");return a==s.npos?"":s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);}
}
bool Deal::operator==(const Deal& b)const{return std::tie(id,amount,term,payment,floating,rate)==std::tie(b.id,b.amount,b.term,b.payment,b.floating,b.rate);}
void validate(const Config& c) {
    if (!c.count || c.count > 1000000) throw std::runtime_error("count: 1..1000000");
    if (c.min_amount < 1 || c.max_amount < c.min_amount || c.max_amount > 1000000000000LL)
        throw std::runtime_error("Invalid amount range");
    if (c.min_term < 1 || c.max_term < c.min_term || c.max_term > 1200)
        throw std::runtime_error("Invalid term range");
    const bool bad_share = std::any_of(c.shares.begin(), c.shares.end(), [](int x) { return x < 0 || x > 10000; });
    if (bad_share || std::accumulate(c.shares.begin(), c.shares.end(), 0) != 10000)
        throw std::runtime_error("Payment shares must sum to 10000");
    if (c.floating < 0 || c.floating > 10000) throw std::runtime_error("Invalid floating share");
    if (c.min_rate < 0 || c.max_rate < c.min_rate || c.max_rate > 100000 ||
        c.min_spread < 0 || c.max_spread < c.min_spread || c.max_spread > 100000)
        throw std::runtime_error("Invalid rates");
    if (c.discount_rate < 0 || c.discount_rate > 100000 || c.index_rate < 0 || c.index_rate > 100000)
        throw std::runtime_error("Invalid discount or index rate");
    (void)cf::parse_date(c.valuation_date);
    if (!(cf::parse_date(c.valuation_date) < cf::parse_date(c.scenario_date)))
        throw std::runtime_error("scenario_date must be after valuation_date");
    if (c.scenario_discount < 0 || c.scenario_discount > 100000 || c.new_count < 0 || c.new_count > 100000)
        throw std::runtime_error("Invalid scenario discount rate or new deal count");
}
Config profile(const std::string& name) {
    Config c;
    if (name == "short") { c.min_term = 1; c.max_term = 12; }
    else if (name == "long") { c.min_term = 60; c.max_term = 240; c.min_amount = 100000000; c.max_amount = 2000000000; }
    else if (name == "floating") c.floating = 9000;
    else if (name != "default") throw std::runtime_error("Unknown profile");
    return c;
}
Config load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read config");
    Config c;
    std::vector<std::string> seen;
    std::string raw;
    int line_no = 0;
    auto fail = [&line_no](const std::string& message) {
        throw std::runtime_error("Config line " + std::to_string(line_no) + ": " + message);
    };
    while (std::getline(file, raw)) {
        ++line_no;
        const auto line = trim(raw);
        if (line.empty() || line[0] == '#') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) fail("expected key=value");
        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) fail("duplicate key '" + key + "'");
        seen.push_back(key);
        if (key == "version") {
            if (value != "1.0.0") fail("unsupported version '" + value + "'");
            continue;
        }
        if (key == "valuation_date" || key == "scenario_date") {
            try { (void)cf::parse_date(value); }
            catch (const std::exception& e) { fail(key + ": " + e.what()); }
            (key == "valuation_date" ? c.valuation_date : c.scenario_date) = value;
            continue;
        }
        long long x = 0;
        std::size_t used = 0;
        try { x = std::stoll(value, &used); } catch (const std::exception&) { used = 0; }
        if (value.empty() || used != value.size() || x < 0)
            fail("key '" + key + "' needs a non-negative integer, got '" + value + "'");
        if (key == "min_amount") c.min_amount = x;
        else if (key == "max_amount") c.max_amount = x;
        else if (key == "seed" || key == "new_seed") {
            if (x > 4294967295LL) fail(key + " is out of range 0..4294967295");
            (key == "seed" ? c.seed : c.new_seed) = static_cast<unsigned>(x);
        } else {
            if (x > 2147483647LL) fail("value of '" + key + "' is too large");
            const int n = static_cast<int>(x);
            if (key == "count") c.count = static_cast<unsigned>(n);
            else if (key == "min_term") c.min_term = n;
            else if (key == "max_term") c.max_term = n;
            else if (key == "ann") c.shares[0] = n;
            else if (key == "simple") c.shares[1] = n;
            else if (key == "rgm") c.shares[2] = n;
            else if (key == "floating") c.floating = n;
            else if (key == "min_rate") c.min_rate = n;
            else if (key == "max_rate") c.max_rate = n;
            else if (key == "min_spread") c.min_spread = n;
            else if (key == "max_spread") c.max_spread = n;
            else if (key == "discount_rate") c.discount_rate = n;
            else if (key == "index_rate") c.index_rate = n;
            else if (key == "scenario_discount") c.scenario_discount = n;
            else if (key == "new_count") c.new_count = n;
            else fail("unknown key '" + key + "'");
        }
    }
    validate(c);
    return c;
}
void save(const Config& c,const std::filesystem::path& p){validate(c);auto f=out(p);f<<"version=1.0.0\ncount="<<c.count<<"\nseed="<<c.seed<<"\nmin_amount="<<c.min_amount<<"\nmax_amount="<<c.max_amount<<"\nmin_term="<<c.min_term<<"\nmax_term="<<c.max_term<<"\nann="<<c.shares[0]<<"\nsimple="<<c.shares[1]<<"\nrgm="<<c.shares[2]<<"\nfloating="<<c.floating<<"\nmin_rate="<<c.min_rate<<"\nmax_rate="<<c.max_rate<<"\nmin_spread="<<c.min_spread<<"\nmax_spread="<<c.max_spread<<"\ndiscount_rate="<<c.discount_rate<<"\nindex_rate="<<c.index_rate<<"\nvaluation_date="<<c.valuation_date<<"\nscenario_date="<<c.scenario_date<<"\nscenario_discount="<<c.scenario_discount<<"\nnew_count="<<c.new_count<<"\nnew_seed="<<c.new_seed<<'\n';}
std::array<std::size_t, 3> quotas(std::size_t n, const std::array<int, 3>& shares) {
    std::array<std::size_t, 3> q{}, remainder{};
    std::size_t sum = 0;
    for (int i = 0; i < 3; ++i) {
        const auto v = std::uint64_t(n) * shares[i];
        q[i] = static_cast<std::size_t>(v / 10000);
        remainder[i] = static_cast<std::size_t>(v % 10000);
        sum += q[i];
    }
    while (sum < n) {
        auto it = std::max_element(remainder.begin(), remainder.end());
        ++q[it - remainder.begin()];
        *it = 0;
        ++sum;
    }
    return q;
}
std::vector<Deal> generate(const Config& c) {
    validate(c);
    Random rng(c.seed);
    auto pay_quota = quotas(c.count, c.shares);
    auto rate_quota = quotas(c.count, {10000 - c.floating, c.floating, 0});
    std::vector<int> payments, rates;
    for (int i = 0; i < 3; ++i) payments.insert(payments.end(), pay_quota[i], i);
    for (int i = 0; i < 2; ++i) rates.insert(rates.end(), rate_quota[i], i);
    rng.shuffle(payments);
    rng.shuffle(rates);
    std::vector<Deal> deals;
    deals.reserve(c.count);
    for (unsigned i = 0; i < c.count; ++i) {
        auto amount = rng.range(c.min_amount, c.max_amount);
        int term = static_cast<int>(rng.range(c.min_term, c.max_term));
        int rate = static_cast<int>(rates[i] ? rng.range(c.min_spread, c.max_spread) : rng.range(c.min_rate, c.max_rate));
        deals.push_back({i + 1, amount, term, payments[i], rates[i], rate});
    }
    return deals;
}
std::string summary(const Config& c,const std::vector<Deal>& d){if(d.empty())throw std::runtime_error("Empty portfolio");std::array<std::size_t,3> n{};std::size_t floating=0;auto amin=c.max_amount,amax=c.min_amount;int tmin=c.max_term,tmax=c.min_term;for(auto& x:d){++n[x.payment];floating+=x.floating;amin=std::min(amin,x.amount);amax=std::max(amax,x.amount);tmin=std::min(tmin,x.term);tmax=std::max(tmax,x.term);}std::ostringstream s;s.imbue(std::locale::classic());s<<std::fixed<<std::setprecision(2)<<"Version 1.0.0 | seed="<<c.seed<<" | count="<<d.size()<<"\nTarget -> Actual (difference: percentage points)\n";for(int i=0;i<3;++i){double p=100.0*n[i]/d.size();s<<names[i]<<": "<<c.shares[i]/100.0<<"% -> "<<p<<"% ("<<p-c.shares[i]/100.0<<" pp)\n";}double p=100.0*floating/d.size();s<<"Floating: "<<c.floating/100.0<<"% -> "<<p<<"% ("<<p-c.floating/100.0<<" pp)\nAmount RUB: ["<<c.min_amount/100.0<<", "<<c.max_amount/100.0<<"] -> ["<<amin/100.0<<", "<<amax/100.0<<"]\nTerm months: ["<<c.min_term<<", "<<c.max_term<<"] -> ["<<tmin<<", "<<tmax<<"]\n";return s.str();}
void report(const Config& c,const std::vector<Deal>& d,const std::filesystem::path& dir){validate(c);if(d.empty())throw std::runtime_error("Empty portfolio");if(std::filesystem::exists(dir)&&!std::filesystem::is_empty(dir))throw std::runtime_error("Output directory must be empty");const auto val=value_portfolio(c,d);const auto fac=analyse_factors(c,d);std::filesystem::create_directories(dir);save(c,dir/"config.ini");{auto f=out(dir/"summary.txt");f<<summary(c,d);} {auto f=out(dir/"portfolio.csv");f<<"id,amount_kopecks,term_months,payment_type,rate_type,fixed_rate_bps,index,spread_bps\n";for(auto& x:d){f<<x.id<<','<<x.amount<<','<<x.term<<','<<names[x.payment]<<','<<(x.floating?"FLOATING":"FIXED")<<',';if(x.floating)f<<",TEST_INDEX,"<<x.rate;else f<<x.rate<<",,";f<<'\n';}}
 {auto g=out(dir/"portfolio_cashflows.csv");g<<cashflows_csv(val);}{auto g=out(dir/"factor_analysis.csv");g<<factors_csv(fac);}auto f=out(dir/"report.html");f<<R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Portfolio</title><style>body{font:16px system-ui;background:#f1f5fb;color:#19283e;max-width:1200px;margin:25px auto;padding:15px}section{background:white;padding:22px;margin:15px 0;border-radius:14px}button,input,select{padding:9px;margin:5px}button,.bar,tr{cursor:pointer}.bar{background:#3b70d5;color:white;margin:6px 0;padding:8px;min-width:100px;box-sizing:border-box}table{width:100%;border-collapse:collapse}td,th{text-align:left;padding:8px;border-bottom:1px solid #ddd}.charts{display:grid;grid-template-columns:repeat(auto-fit,minmax(250px,1fr));gap:22px}pre{white-space:pre-wrap}</style><h1>Генератор кредитного портфеля</h1><p>Тестовые равномерные распределения. Доли по количеству сделок. Плавающая ставка = TEST_INDEX + надбавка.</p><section><h2>Задано → Получилось</h2><pre>)HTML"<<summary(c,d)<<R"HTML(</pre><a href="portfolio.csv" download>Весь CSV</a> · <a href="config.ini" download>Сохранить настройки</a></section>)HTML"<<valuation_html(val)<<factors_html(fac)<<R"HTML(<section><h2>Распределения</h2><p>Нажмите столбец для перехода к сделкам. Подпись показывает заданную долю, столбец — фактическую.</p><div id="charts" class="charts"></div></section><section><h2>Сделки</h2><p id="filter"></p><button id="reset">Сбросить</button><button id="download">CSV выборки</button><input id="search" placeholder="ID сделки"><select id="payment"><option value="">Все платежи</option><option>ANN</option><option>SIMPLE</option><option>RGM</option></select><select id="rate"><option value="">Все ставки</option><option>FIXED</option><option>FLOATING</option></select><p id="count"></p><table><thead><tr><th>ID</th><th>Сумма, ₽</th><th>Месяцы</th><th>Платежи</th><th>Ставка</th></tr></thead><tbody id="rows"></tbody></table><button id="prev">Назад</button><span id="page"></span><button id="next">Вперёд</button></section><dialog id="details"><pre id="detail"></pre><button onclick="this.parentElement.close()">Закрыть</button></dialog><script>const data=[
)HTML";for(auto& x:d)f<<'['<<x.id<<','<<x.amount<<','<<x.term<<','<<x.payment<<','<<x.floating<<','<<x.rate<<"],\n";
 f<<"];const targets=["<<c.shares[0]/100.0<<','<<c.shares[1]/100.0<<','<<c.shares[2]/100.0<<"],floating="<<c.floating/100.0<<",amin="<<c.min_amount<<",amax="<<c.max_amount<<",tmin="<<c.min_term<<",tmax="<<c.max_term<<";\n";
 f<<R"HTML(const names=['ANN','SIMPLE','RGM'],el=id=>document.getElementById(id);let test=()=>true,label='Все сделки',page=0,filtered=[];const size=50;
function render(){const p=el('payment').value,r=el('rate').value,id=el('search').value.trim();filtered=data.filter(x=>test(x)&&(!p||names[x[3]]===p)&&(!r||(x[4]?'FLOATING':'FIXED')===r)&&(!id||String(x[0])===id));page=Math.max(0,Math.min(page,Math.ceil(filtered.length/size)-1));el('filter').textContent=label;el('count').textContent='Найдено: '+filtered.length;el('page').textContent=(page+1)+' / '+Math.max(1,Math.ceil(filtered.length/size));el('rows').replaceChildren();for(const x of filtered.slice(page*size,(page+1)*size)){const tr=document.createElement('tr');for(const v of [x[0],(x[1]/100).toLocaleString('ru-RU'),x[2],names[x[3]],x[4]?'TEST_INDEX + '+x[5]/100+' п.п.':x[5]/100+'%']){const td=document.createElement('td');td.textContent=v;tr.append(td)}tr.onclick=()=>{el('detail').textContent=JSON.stringify({id:x[0],amount_kopecks:x[1],term_months:x[2],payment:names[x[3]],rate_type:x[4]?'FLOATING':'FIXED',fixed_rate_bps:x[4]?null:x[5],index:x[4]?'TEST_INDEX':null,spread_bps:x[4]?x[5]:null},null,2);el('details').showModal()};el('rows').append(tr)}el('prev').disabled=page===0;el('next').disabled=(page+1)*size>=filtered.length;}
function chart(title,groups){const box=document.createElement('div'),h=document.createElement('h3');h.textContent=title;box.append(h);for(const g of groups){const n=data.filter(g.test).length,p=100*n/data.length,t=document.createElement('div');t.textContent=g.name+' — задано '+g.target.toFixed(2)+'%';box.append(t);const b=document.createElement('div');b.className='bar';b.style.width=Math.max(8,p)+'%';b.textContent=p.toFixed(2)+'% ('+n+')';b.onclick=()=>{test=g.test;label=title+': '+g.name;page=0;render();el('filter').scrollIntoView({behavior:'smooth'})};box.append(b)}el('charts').append(box)}
chart('Платежи',names.map((name,i)=>({name,target:targets[i],test:x=>x[3]===i})));chart('Ставки',[{name:'FIXED',target:100-floating,test:x=>!x[4]},{name:'FLOATING',target:floating,test:x=>!!x[4]}]);
function bins(title,lo,hi,col,money){const width=hi-lo+1,n=Math.min(5,width),groups=[];for(let i=0;i<n;i++){const a=lo+Math.ceil(i*width/n),b=lo+Math.ceil((i+1)*width/n)-1;groups.push({name:money?(a/100).toFixed(2)+'–'+(b/100).toFixed(2)+' ₽':a+'–'+b+' мес.',target:100*(b-a+1)/width,test:x=>x[col]>=a&&x[col]<=b})}chart(title,groups)}bins('Суммы',amin,amax,1,true);bins('Сроки',tmin,tmax,2,false);
for(const id of ['search','payment','rate'])el(id).oninput=()=>{page=0;render()};el('reset').onclick=()=>{test=()=>true;label='Все сделки';page=0;for(const id of ['search','payment','rate'])el(id).value='';render()};el('prev').onclick=()=>{page--;render()};el('next').onclick=()=>{page++;render()};el('download').onclick=()=>{const csv='id,amount_kopecks,term_months,payment_type,rate_type,fixed_rate_bps,index,spread_bps\n'+filtered.map(x=>[x[0],x[1],x[2],names[x[3]],x[4]?'FLOATING':'FIXED',x[4]?'':x[5],x[4]?'TEST_INDEX':'',x[4]?x[5]:''].join(',')).join('\n');const url=URL.createObjectURL(new Blob([csv],{type:'text/csv;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='selected.csv';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)};render();</script></html>)HTML";
}
}
