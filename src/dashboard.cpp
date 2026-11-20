#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace fs=std::filesystem;
using Row=std::map<std::string,std::string>;
void require(bool b,const std::string& s){if(!b)throw std::runtime_error(s);}
std::vector<std::string> split(const std::string& s){std::vector<std::string> a;std::string v;bool quote=false,closed=false;for(std::size_t i=0;i<s.size();++i){char c=s[i];if(quote){if(c=='"'){if(i+1<s.size()&&s[i+1]=='"'){v+='"';++i;}else{quote=false;closed=true;}}else v+=c;}else if(c==','){a.push_back(v);v.clear();closed=false;}else if(c=='"'){require(v.empty()&&!closed,"Invalid CSV quote");quote=true;}else{require(!closed,"Text after CSV quote");v+=c;}}require(!quote,"Unterminated CSV field");a.push_back(v);return a;}
std::vector<Row> csv(const fs::path& p,const std::vector<std::string>& required){std::ifstream f(p,std::ios::binary);require(bool(f),"Cannot read input CSV");std::string s;require(bool(std::getline(f,s)),"Empty CSV");if(s.size()>=3&&s.substr(0,3)=="\xef\xbb\xbf")s.erase(0,3);if(!s.empty()&&s.back()=='\r')s.pop_back();auto h=split(s);std::set<std::string> unique(h.begin(),h.end());require(unique.size()==h.size(),"Duplicate columns");for(auto& k:required)require(unique.count(k)!=0,"Missing column: "+k);std::vector<Row> rows;while(std::getline(f,s)){if(!s.empty()&&s.back()=='\r')s.pop_back();if(s.empty())continue;auto a=split(s);require(a.size()==h.size(),"CSV field count mismatch");Row row;for(std::size_t i=0;i<h.size();++i)row[h[i]]=a[i];rows.push_back(row);}require(!rows.empty(),"No data rows");return rows;}
long double number(const std::string& s){require(!s.empty(),"Empty number");std::size_t n=0;auto x=std::stold(s,&n);require(n==s.size()&&std::isfinite(x),"Invalid number");return x;}
long long integer(const std::string& s){require(!s.empty()&&std::all_of(s.begin(),s.end(),[](char c){return c>='0'&&c<='9';}),"Invalid integer");return std::stoll(s);}
void date(const std::string& s){require(s.size()==10&&s[4]=='-'&&s[7]=='-',"Invalid date");auto y=integer(s.substr(0,4)),m=integer(s.substr(5,2)),d=integer(s.substr(8,2));require(y>=1&&m>=1&&m<=12,"Invalid date");int days[]={31,28,31,30,31,30,31,31,30,31,30,31};if(y%4==0&&(y%100!=0||y%400==0))days[1]=29;require(d>=1&&d<=days[m-1],"Invalid day");}
std::string json(const std::string& s){std::string o="\"";for(unsigned char c:s){switch(c){case '"':o+="\\\"";break;case '\\':o+="\\\\";break;case '<':o+="\\u003c";break;case '>':o+="\\u003e";break;case '&':o+="\\u0026";break;default:if(c<32){const char* hex="0123456789abcdef";o+="\\u00";o+=hex[c>>4];o+=hex[c&15];}else o+=static_cast<char>(c);}}return o+'"';}
struct Payment{long double principal=0,interest=0;};
struct Deal{long double opening=0,principal=0,interest=0,closing=0,current=0;long long count=0,actual_count=0;std::string source;std::map<std::string,Payment> dates;};
int run_dashboard(int argc,char** argv){try{std::locale::global(std::locale::classic());require(argc==3||argc==4,"Usage: dashboard INPUT_DIR OUTPUT.html [--check]");auto input=fs::u8path(argv[1]);bool check=argc==4;require(!check||std::string(argv[3])=="--check","Unknown option");
 auto summaries=csv(input/"deal_summary.csv",{"instrument_id","opening_principal","total_principal","total_interest","closing_balance","flow_count","current_interest","current_interest_source"});std::map<std::string,Deal> deals;
 for(auto& r:summaries){auto id=r.at("instrument_id");require(!id.empty()&&deals.count(id)==0,"Empty or duplicate instrument_id");Deal d;d.opening=number(r.at("opening_principal"));d.principal=number(r.at("total_principal"));d.interest=number(r.at("total_interest"));d.closing=number(r.at("closing_balance"));d.current=number(r.at("current_interest"));d.count=integer(r.at("flow_count"));d.source=r.at("current_interest_source");require(d.source=="COMPUTED"||d.source=="REPORTED","Unknown current_interest_source");deals[id]=d;}
 auto flows=csv(input/"cashflows.csv",{"instrument_id","payment_date","cash_flow_type","amount","rate_used","year_fraction"});
 for(auto& r:flows){auto id=r.at("instrument_id"),day=r.at("payment_date"),type=r.at("cash_flow_type");require(deals.count(id)!=0,"Unknown instrument_id in CF");date(day);auto amount=number(r.at("amount"));number(r.at("rate_used"));require(number(r.at("year_fraction"))>=0,"Negative year_fraction");require(type=="PRINCIPAL"||type=="INTEREST","Unknown CF type: "+type);auto& d=deals.at(id);++d.actual_count;auto& p=d.dates[day];if(type=="PRINCIPAL")p.principal+=amount;else p.interest+=amount;}
 long double max_error=0;for(auto& item:deals){auto& d=item.second;long double principal=0,interest=0;for(auto& p:d.dates){principal+=p.second.principal;interest+=p.second.interest;}max_error=std::max({max_error,std::abs(principal-d.principal),std::abs(interest-d.interest)});require(std::abs(principal-d.principal)<=0.005L,"Principal reconciliation failed: "+item.first);require(std::abs(interest-d.interest)<=0.005L,"Interest reconciliation failed: "+item.first);require(d.count==d.actual_count,"flow_count mismatch: "+item.first);require(std::abs(d.opening-d.principal-d.closing)<=0.005L,"Balance mismatch: "+item.first);}
 std::cout<<"Validated deals="<<deals.size()<<" CF rows="<<flows.size()<<"\n";
 if(check){ require(deals.count("1")&&deals.at("1").dates.count("2022-09-22"),"Control payment missing");auto control=deals.at("1").dates.at("2022-09-22");require(std::llround((control.principal+control.interest)*100)==2603732,"Control payment != 26037.32");std::cout<<"Validated deals="<<deals.size()<<" CF rows="<<flows.size()<<" max reconciliation error="<<static_cast<double>(max_error)<<"\nControl 1 / 2022-09-22: "<<std::fixed<<std::setprecision(2)<<control.principal+control.interest<<"\n";
require(deals.size()==1&&flows.size()==2,"Fixture size changed");require(split("a,\"b,c\",\"d\"\"e\"")==std::vector<std::string>{"a","b,c","d\"e"},"CSV parser test");bool invalid=false;try{date("2023-02-29");}catch(...){invalid=true;}require(invalid,"Date test");std::cout<<"Integration tests passed\n";return 0;}
 auto path=fs::u8path(argv[2]);require(!fs::exists(path),"Output exists; choose a new file");if(!path.parent_path().empty())fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);require(bool(f),"Cannot write dashboard");f.exceptions(std::ios::badbit|std::ios::failbit);f<<std::setprecision(18);
 f<<R"HTML(<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Практика — дашборд</title><style>body{font:16px system-ui;margin:25px auto;max-width:1200px;padding:15px;background:#eff3fa;color:#1b2940}section,.card{padding:20px;margin:16px 0;background:white;border-radius:14px}button,select,input{padding:9px;margin:5px}button{cursor:pointer}.demo{background:#fff0c2;padding:15px;border-radius:8px}table{border-collapse:collapse;width:100%}th,td{padding:9px;border-bottom:1px solid #ddd;text-align:left}pre{white-space:pre-wrap}.bar{background:#4378dc;color:white;padding:9px;margin:8px 0;cursor:pointer;min-width:120px;box-sizing:border-box}.negative{background:#bc475b}nav{position:sticky;top:0;background:#eff3fa;padding:10px}a{color:#245cc4}</style><h1>Кредитный портфель</h1><nav><button onclick="tab('payments')">Платежи · CF</button><button onclick="tab('factors')">Факторный анализ · ДЕМО</button><button onclick="tab('generator')">Генератор</button></nav>
<section id="payments"><h2>Платежи первой команды</h2><p>Источник: deal_summary.csv + cashflows.csv. Сверка сумм и числа строк пройдена. NPV по базе и стрессу ещё не предоставлен — здесь не рассчитывается.</p><label>Сделка <select id="deal"></select></label><div id="summary"></div><label>С <input type="date" id="from"></label><label>По <input type="date" id="to"></label><button id="export">CSV платежей</button><p id="status"></p><table><thead><tr><th>Дата</th><th>Тело</th><th>Проценты</th><th>Итого</th></tr></thead><tbody id="rows"></tbody></table></section>
<section id="factors" hidden><h2>Факторный анализ</h2><p class="demo">ДЕМОНСТРАЦИОННЫЙ ПРИМЕР. NPV, вклады и взаимодействия заданы вручную для защиты. Они не рассчитаны на загруженных CF и не являются результатом модели. Все суммы демопримера — ₽.</p><label>Период до <select id="before"><option>2026-08</option><option>2026-09</option></select></label><label>Период после <select id="after"><option>2026-09</option><option>2026-10</option></select></label><div id="npv"></div><div id="bars"></div><div class="card" id="factorDetail">Нажмите на фактор для подробностей.</div></section>
<section id="generator" hidden><h2>Генератор на C++</h2><p>Синтетический набор отделён от CF первой команды. Совпадающие ID не связывают эти наборы.</p><a href="../demo/report.html">Открыть существующий отчёт генератора</a><p>Настройки/профили меняются через INI; запуск генерации пока через CLI. Веб-форма запуска ещё не реализована.</p><pre>.\build\Release\portfolio.exe --profile short --out run-new
.\build\Release\portfolio.exe --config run-new/config.ini --out replay-new</pre></section><script>const deals={
)HTML";
 for(auto& item:deals){auto& d=item.second;f<<json(item.first)<<":{opening:"<<d.opening<<",principal:"<<d.principal<<",interest:"<<d.interest<<",closing:"<<d.closing<<",current:"<<d.current<<",source:"<<json(d.source)<<",count:"<<d.count<<",dates:[";for(auto& p:d.dates)f<<'['<<json(p.first)<<','<<p.second.principal<<','<<p.second.interest<<"],";f<<"]},\n";}
 f<<R"HTML(};const el=id=>document.getElementById(id),money=x=>x.toLocaleString('ru-RU',{minimumFractionDigits:2,maximumFractionDigits:2});let visible=[];
function tab(id){for(const x of ['payments','factors','generator'])el(x).hidden=x!==id}for(const id of Object.keys(deals).sort((a,b)=>Number(a)-Number(b))){const o=document.createElement('option');o.value=id;o.textContent=id;el('deal').append(o)}
function payments(){const id=el('deal').value,d=deals[id];el('summary').textContent='Начальное тело: '+money(d.opening)+' | Погашение: '+money(d.principal)+' | Проценты: '+money(d.interest)+' | Остаток: '+money(d.closing)+' | Строк CF: '+d.count+' | Текущие проценты (отдельно, не прибавляются к CF): '+money(d.current)+' ['+d.source+']';const from=el('from').value,to=el('to').value;el('rows').replaceChildren();if(from&&to&&from>to){visible=[];el('status').textContent='Ошибка: начало диапазона позже конца';return}visible=d.dates.filter(x=>(!from||x[0]>=from)&&(!to||x[0]<=to));let p=0,i=0;for(const x of visible){p+=x[1];i+=x[2];const tr=document.createElement('tr');for(const v of [x[0],money(x[1]),money(x[2]),money(x[1]+x[2])]){const td=document.createElement('td');td.textContent=v;tr.append(td)}el('rows').append(tr)}el('status').textContent='Платёжных дат: '+visible.length+' | Тело: '+money(p)+' | Проценты: '+money(i)+' | Итого: '+money(p+i)}
for(const id of ['deal','from','to'])el(id).onchange=payments;el('export').onclick=()=>{const s='instrument_id,payment_date,principal,interest,total\n'+visible.map(x=>[el('deal').value,x[0],x[1],x[2],x[1]+x[2]].join(',')).join('\n'),url=URL.createObjectURL(new Blob([s],{type:'text/csv;charset=utf-8'})),a=document.createElement('a');a.href=url;a.download='cashflows-selected.csv';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)};
const snapshots={'2026-08':100000000,'2026-09':97000000,'2026-10':98500000};const examples={
'2026-08/2026-09':{factors:[{name:'Сдвиг кривой',value:-4000000,detail:'Заданный демовклад изменения кривой дисконтирования.'},{name:'Изменение состава',value:1500000,detail:'Заданный демовклад изменения состава портфеля.'},{name:'Прохождение времени',value:200000,detail:'Заданный демовклад прохождения времени.'}],interactions:[{name:'Кривая × состав',value:-500000,detail:'Отдельный демовклад взаимодействия; не включён повторно во вклады факторов.'}],residual:-200000},
'2026-09/2026-10':{factors:[{name:'Сдвиг кривой',value:1000000,detail:'Заданный демовклад изменения кривой.'},{name:'Изменение состава',value:400000,detail:'Заданный демовклад изменения состава.'},{name:'Прохождение времени',value:50000,detail:'Заданный демовклад времени.'}],interactions:[],residual:50000},
'2026-08/2026-10':{factors:[{name:'Сдвиг кривой',value:-3000000,detail:'Заданный демовклад для выбранной пары периодов.'},{name:'Изменение состава',value:1900000,detail:'Заданный демовклад состава.'},{name:'Прохождение времени',value:250000,detail:'Заданный демовклад времени.'}],interactions:[{name:'Кривая × состав',value:-500000,detail:'Отдельно учтённое демонстрационное взаимодействие.'}],residual:-150000}};
function factors(){const a=el('before').value,b=el('after').value,e=examples[a+'/'+b];el('bars').replaceChildren();el('factorDetail').textContent='Нажмите вклад для подробностей.';if(!e){el('npv').textContent='Для такой пары периодов демопример не подготовлен. Выберите период после, следующий за периодом до.';return}const delta=snapshots[b]-snapshots[a],sum=e.factors.reduce((s,x)=>s+x.value,0)+e.interactions.reduce((s,x)=>s+x.value,0)+e.residual;el('npv').textContent='NPV до: '+money(snapshots[a])+' → после: '+money(snapshots[b])+' | Изменение: '+money(delta)+' | Остаток: '+money(e.residual)+' | Контроль баланса: '+(sum===delta?'OK':'ОШИБКА');const items=[...e.factors,...e.interactions,{name:'Необъяснённый остаток',value:e.residual,detail:'Разница между изменением NPV и суммой переданных вкладов; не выдумываем объяснение.'}];for(const x of items){const bar=document.createElement('div');bar.className='bar'+(x.value<0?' negative':'');bar.style.width=Math.max(15,Math.abs(x.value)/4000000*100)+'%';bar.textContent=x.name+': '+money(x.value);bar.onclick=()=>{el('factorDetail').textContent=x.name+': '+money(x.value)+'. '+x.detail+' Взаимодействия выбранного примера: '+(e.interactions.length?e.interactions.map(i=>i.name+' = '+money(i.value)).join('; '):'не переданы')};el('bars').append(bar)}}
for(const id of ['before','after'])el(id).onchange=factors;payments();factors();</script></html>)HTML";f.close();std::cout<<"Dashboard written\n";return 0;}catch(const std::exception& e){std::cerr<<"Error: "<<e.what()<<'\n';return 1;}}

#ifdef _WIN32
int wmain(int argc,wchar_t** argv){
 try{
  std::vector<std::string> storage;
  storage.reserve(static_cast<std::size_t>(argc));
  for(int i=0;i<argc;++i){
   int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);
   require(n>0,"Cannot encode Windows argument");
   std::string value(static_cast<std::size_t>(n),'\0');
   require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,value.data(),n,nullptr,nullptr)>0,"Cannot encode Windows argument");
   value.pop_back();storage.push_back(value);
  }
  std::vector<char*> args;
  for(auto& value:storage)args.push_back(value.data());
  args.push_back(nullptr);
  return run_dashboard(argc,args.data());
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
#else
int main(int argc,char** argv){return run_dashboard(argc,argv);}
#endif
