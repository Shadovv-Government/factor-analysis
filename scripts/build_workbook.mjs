import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { createRequire } from 'node:module';
import { fork } from 'node:child_process';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const require = createRequire(path.join(root, '.runtime', 'package.json'));
const { Workbook, SpreadsheetFile } = await import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
const out = path.resolve(process.argv[2] || path.join(root, 'outputs', 'sprint1_20261009'));
const payload = JSON.parse(await fs.readFile(path.join(out, 'calculation.json'), 'utf8'));
const cases = JSON.parse(await fs.readFile(path.join(out, 'validation_cases.json'), 'utf8'));
const kinds = ['ANN', 'SIMPLE', 'RGM'];
if (kinds.some(k => !payload.results[k])) throw new Error('Workbook comparison requires ANN, SIMPLE, RGM');
const n = payload.results.ANN.payments.length;
if (kinds.some(k => payload.results[k].payments.length !== n)) throw new Error('Unaligned calendars');
const first = 7, last = first + n - 1;
const wb = Workbook.create();
const summary = wb.worksheets.add('Сравнение');
const params = wb.worksheets.add('Параметры');
const sheets = Object.fromEntries(kinds.map(k => [k, wb.worksheets.add(k)]));
const moneyFormat = '#,##0.00;(#,##0.00);0.00';
const pvDiffFormat = '0.00000000;(0.00000000);0.00000000';
const palette = ['#2364AA', '#D47B28', '#24856D'];
const serialDate = iso => (Date.parse(`${iso}T00:00:00Z`) - Date.UTC(1899, 11, 30)) / 86400000;
const d = payload.request.deal;
const disc = payload.request.discounting;

function styleBase(sheet, range) {
  sheet.showGridLines = false;
  sheet.getRange(range).format.font = { name: 'Arial', size: 10, color: '#243447' };
  sheet.getRange(range).format.verticalAlignment = 'center';
  sheet.getRange(range).format.rowHeight = 21;
  sheet.getRange(range).format.columnWidth = 16;
  sheet.getRange('A:A').format.columnWidth = 3;
}
function header(sheet, range) {
  sheet.getRange(range).format = {
    fill: '#203B5C', font: { name: 'Arial', size: 10, color: '#FFFFFF', bold: true },
    horizontalAlignment: 'center', verticalAlignment: 'center', wrapText: true,
    rowHeight: 36,
    borders: { insideVertical: { color: '#FFFFFF', style: 'thin' } }
  };
}
function title(sheet, value) {
  sheet.getRange('B2').values = [[value]];
  sheet.getRange('B2').format.font = { name: 'Arial', size: 14, bold: true, color: '#203B5C' };
  sheet.getRange('B2:K2').format.rowHeight = 28;
  sheet.getRange('B2:K2').format.borders = { bottom: { color: '#B9C8D7', style: 'thin' } };
}
function formula(sheet, cell, value, linked = false) {
  sheet.getRange(cell).formulas = [[value]];
  sheet.getRange(cell).format.font.color = linked ? '#22704B' : '#243447';
}

styleBase(params, 'A1:H23');
params.getRange('B:B').format.columnWidth = 39;
params.getRange('C:C').format.columnWidth = 25;
params.getRange('D:D').format.columnWidth = 3;
params.getRange('E:H').format.columnWidth = 24;
title(params, 'Параметры сделки и метода');
params.getRange('B4:C16').values = [
  ['Параметр', 'Значение'],
  ['Дата оценки (фиксирована)', serialDate(d.valuation_date)],
  ['Остаток долга, RUB', Number(d.outstanding_principal)],
  ['Кредитная ставка', Number(d.annual_credit_rate)],
  ['Дисконтная ставка', Number(disc.annual_rate)],
  ['Первый платёж (фиксирован)', serialDate(d.first_payment_date)],
  ['Погашение (фиксировано)', serialDate(d.maturity_date)],
  ['Период, месяцев (фиксирован)', d.frequency_months],
  ['Количество платежей (фиксировано)', n],
  ['Регулярное тело RGM, RUB', null],
  ['Регулярный платёж ANN, RUB', null],
  ['База дней', 'ACT/365F'],
  ['Округление денег', 'HALF_UP / 2 знака']
];
header(params, 'B4:C4');
params.getRange('C5').setNumberFormat('yyyy-mm-dd');
params.getRange('C9:C10').setNumberFormat('yyyy-mm-dd');
params.getRange('C6').setNumberFormat(moneyFormat);
params.getRange('C7:C8').setNumberFormat('0.00%');
params.getRange('C13:C14').setNumberFormat(moneyFormat);
params.getRange('C6:C8').format.fill = '#FFF2CC';
params.getRange('C6:C8').format.font.color = '#2364AA';
formula(params, 'C13', '=IF(AND(ISNUMBER(C6),C6>0,ROUND(C6,2)=C6),ROUND(C6/C12,2),NA())');
formula(params, 'C14', `=ROUND(C6/SUM(ANN!J${first}:J${last}),2)`, true);
params.getRange('B18').values = [['Жёлтые ячейки: долг и две ставки можно менять.']];
params.getRange('B19').values = [['Даты и число платежей заданы для этой сделки.']];
params.getRange('B20').values = [['Другой календарь: измените JSON и повторите сборку.']];
params.getRange('B22').values = [['Источник: annuity-payment-schedule.xlsx, H9:I20.']];
params.getRange('E5').values = [['Дисконтирование: DF предыдущего периода / (1 + ставка × дни / 365).']];
params.getRange('E7').values = [['Ставки задаются долями единицы: 0,075 означает 7,5%.']];
params.getRange('E9').values = [['SIMPLE: проценты периодически, тело в конце.']];
params.getRange('E10').values = [['RGM: равные части тела, последний платёж закрывает остаток.']];
params.getRange('E12').values = [['Без переноса выходных, комиссий и риска дефолта.']];

for (const [kind, sheet] of Object.entries(sheets)) {
  styleBase(sheet, `A1:O${last}`);
  sheet.getRange('A:A').format.columnWidth = 7;
  sheet.getRange('B:B').format.columnWidth = 14;
  sheet.getRange('C:C').format.columnWidth = 7;
  sheet.getRange('D:H').format.columnWidth = 17;
  sheet.getRange('I:L').format.columnWidth = 16;
  sheet.getRange('M:O').format.columnWidth = 18;
  title(sheet, `${kind} — график и приведённая стоимость`);
  sheet.getRange('B3').values = [['Суммы в RUB; ACT/365F; деньги округлены до копеек.']];
  sheet.getRange('B5:O5').values = [[
    'Дата', 'Дней', 'Долг до', 'Проценты', 'Тело', 'Платёж', 'Долг после',
    'Ставка кредита за период', 'DF кредита', 'Ставка дисконта за период', 'DF дисконта',
    'PV процентов', 'PV тела', 'PV платежа'
  ]];
  sheet.getRange('A5').values = [['№']];
  header(sheet, 'A5:O5');
  sheet.getRange('A6:O6').values = [[0, serialDate(d.valuation_date), 0, 0, 0, 0, 0, null, 0, 1, 0, 1, 0, 0, 0]];
  formula(sheet, 'H6', `=IF(AND(ISNUMBER('Параметры'!$C$6),'Параметры'!$C$6>0,ROUND('Параметры'!$C$6,2)='Параметры'!$C$6),'Параметры'!$C$6,NA())`, true);
  sheet.getRange(`A${first}:B${last}`).values = payload.results[kind].payments.map(r => [r.number, serialDate(r.date)]);
  const matrix = [];
  for (let r = first; r <= last; r++) {
    const prev = r-1;
    const final = r === last;
    const interest = `=ROUND(D${r}*I${r},2)`;
    let principal, payment;
    if (kind === 'ANN') {
      payment = final ? `=ROUND(D${r}+E${r},2)` : `=MIN('Параметры'!$C$14,ROUND(D${r}+E${r},2))`;
      principal = `=IF(G${r}>=E${r},ROUND(G${r}-E${r},2),NA())`;
    } else {
      principal = final ? `=D${r}` : kind === 'SIMPLE' ? '=0' : `=MIN(D${r},'Параметры'!$C$13)`;
      payment = `=ROUND(E${r}+F${r},2)`;
    }
    matrix.push([
      `=B${r}-B${prev}`, `=H${prev}`, interest, principal, payment,
      `=ROUND(D${r}-F${r},2)`,
      `=IF(AND(ISNUMBER('Параметры'!$C$7),'Параметры'!$C$7>=0),'Параметры'!$C$7*C${r}/365,NA())`,
      `=J${prev}/(1+I${r})`,
      `=IF(AND(ISNUMBER('Параметры'!$C$8),'Параметры'!$C$8>=0),'Параметры'!$C$8*C${r}/365,NA())`,
      `=L${prev}/(1+K${r})`,
      `=E${r}*L${r}`, `=F${r}*L${r}`, `=G${r}*L${r}`
    ]);
  }
  sheet.getRange(`C${first}:O${last}`).formulas = matrix;
  sheet.getRange(`B6:B${last}`).setNumberFormat('yyyy-mm-dd');
  sheet.getRange(`D6:H${last}`).setNumberFormat(moneyFormat);
  sheet.getRange(`I6:L${last}`).setNumberFormat('0.000000000');
  sheet.getRange(`M6:O${last}`).setNumberFormat(moneyFormat);
  sheet.getRange(`A6:C${last}`).format.horizontalAlignment = 'center';
  sheet.getRange(`D6:O${last}`).format.horizontalAlignment = 'right';
  sheet.getRange(`I${first}:I${last}`).format.font.color = '#22704B';
  sheet.getRange(`K${first}:K${last}`).format.font.color = '#22704B';
  sheet.getRange(`D${last}:O${last}`).format.fill = '#EAF0F6';
  sheet.getRange(`D${last}:O${last}`).format.font.bold = true;
  sheet.freezePanes.freezeRows(5);
  sheet.freezePanes.freezeColumns(2);
}

styleBase(summary, 'A1:W70');
summary.getRange('B:B').format.columnWidth = 12;
summary.getRange('C:C').format.columnWidth = 9;
summary.getRange('D:G').format.columnWidth = 19;
summary.getRange('H:H').format.columnWidth = 22;
summary.getRange('I:K').format.columnWidth = 19;
summary.getRange('L:L').format.columnWidth = 3;
title(summary, 'Сравнение денежных потоков');
summary.getRange('B3').values = [[`Сделка ${d.id}; суммы в RUB; оценка ${d.valuation_date}.`]];
summary.getRange('B5:K5').values = [[
  'Тип', 'Платежей', 'Всего выплат', 'Проценты', 'Тело', 'PV потоков',
  'PV минус долг', 'Первый платёж', 'Последний платёж', 'Конечный остаток'
]];
header(summary, 'B5:K5');
for (let i=0; i<3; i++) {
  const k=kinds[i], r=6+i;
  summary.getRange(`B${r}`).values = [[k]];
  summary.getRange(`C${r}:K${r}`).formulas = [[
    `=COUNT(${k}!A${first}:A${last})`, `=SUM(${k}!G${first}:G${last})`,
    `=SUM(${k}!E${first}:E${last})`, `=SUM(${k}!F${first}:F${last})`,
    `=SUM(${k}!O${first}:O${last})`, `=G${r}-'Параметры'!C6`,
    `=${k}!G${first}`, `=${k}!G${last}`, `=${k}!H${last}`
  ]];
  summary.getRange(`B${r}:K${r}`).format.rowHeight = 27;
  if(i%2===0) summary.getRange(`B${r}:K${r}`).format.fill='#EDF2F7';
}
summary.getRange('D6:K8').setNumberFormat(moneyFormat);
summary.getRange('H6:H8').setNumberFormat(pvDiffFormat);
summary.getRange('C6:K8').format.horizontalAlignment = 'right';
summary.getRange('B10').values = [['Долг на дату оценки, RUB']];
formula(summary,'D10',"='Параметры'!C6");
summary.getRange('D10').setNumberFormat(moneyFormat);
summary.getRange('F10').values = [['Кредитная ставка']];
formula(summary,'H10',"='Параметры'!C7");
summary.getRange('F11').values = [['Дисконтная ставка']];
formula(summary,'H11',"='Параметры'!C8");
summary.getRange('H10:H11').setNumberFormat('0.00%');
summary.getRange('B13').values = [['PV относится к будущим потокам. PV минус долг — сравнение с остатком, а не полный NPV от выдачи.']];
summary.getRange('B14').values = [['При одинаковых кредитной и дисконтной ставках PV близка к долгу; отклонения вызваны округлением.']];
summary.getRange('B16').values = [['Изменяйте долг и ставки на листе «Параметры»; даты фиксированы для этой сделки.']];
summary.getRange('B18').values = [['Проверки погашения']];
summary.getRange('B18').format.font.bold = true;
summary.getRange('B19:E22').values = [['Тип','Разница тела с долгом','Конечный остаток','Разница платежа и частей'],...kinds.map(k=>[k,null,null,null])];
header(summary,'B19:E19');
summary.getRange('C19:E19').format.rowHeight = 40;
for(let i=0;i<3;i++) {
  const k=kinds[i], r=20+i;
  summary.getRange(`C${r}:E${r}`).formulas = [[
    `=ROUND(SUM(${k}!F${first}:F${last})-'Параметры'!C6,2)`,
    `=${k}!H${last}`,
    `=ROUND(SUM(${k}!G${first}:G${last})-SUM(${k}!E${first}:E${last})-SUM(${k}!F${first}:F${last}),2)`
  ]];
}
summary.getRange('C20:E22').setNumberFormat('0.00');
summary.getRange('C20:E22').conditionalFormats.addCustom('ABS(C20)>0.005', {fill:'#FCE7E7',font:{color:'#A32020',bold:true}});
summary.getRange('B24').values = [['Проверки наблюдают расчёт; основные результаты от этих ячеек не зависят.']];
summary.getRange('B26').values = [['SIMPLE: последний платёж содержит весь долг и поэтому значительно больше регулярных.']];

const helpersStart=73, helpersLast=helpersStart+n+1;
summary.getRange(`B${helpersStart}:E${helpersStart}`).values = [['№','ANN','SIMPLE','RGM']];
summary.getRange(`J${helpersStart}:M${helpersStart}`).values = [['№','ANN','SIMPLE','RGM']];
for(let i=0;i<=n;i++) {
  const r=helpersStart+1+i, source=6+i;
  summary.getRange(`B${r}:E${r}`).formulas = [[`=ANN!A${source}`,`=ANN!H${source}`,`=SIMPLE!H${source}`,`=RGM!H${source}`]];
  if(i<n-1) {
    const cashrow=7+i;
    summary.getRange(`J${r}:M${r}`).formulas = [[`=ANN!A${cashrow}`,`=ANN!G${cashrow}`,`=SIMPLE!G${cashrow}`,`=RGM!G${cashrow}`]];
  }
}
summary.getRange(`B${helpersStart}:M${helpersLast}`).format.font={name:'Arial',size:10,color:'#22704B'};
summary.getRange(`C${helpersStart+1}:E${helpersLast}`).setNumberFormat(moneyFormat);
summary.getRange(`K${helpersStart+1}:M${helpersLast}`).setNumberFormat(moneyFormat);
summary.getRange('B71').values = [['Ниже — ссылки на расчётные листы для редактируемых диаграмм.']];

function chartStyle(chart, chartTitle, from, to, multi=false) {
  chart.setPosition(from,to);
  chart.title=chartTitle;
  chart.titleTextStyle.fontSize=14;
  chart.titleTextStyle.typeface='Arial';
  chart.legend={position:'bottom',textStyle:{typeface:'Arial',fontSize:12}};
  chart.hasLegend=multi;
  chart.xAxis={axisType:'textAxis',textStyle:{typeface:'Arial',fontSize:11},tickLabelInterval:multi ? 50 : 1};
  chart.yAxis={numberFormatCode:'#,##0',numberFormatSourceLinked:false,textStyle:{typeface:'Arial',fontSize:11}};
  if(multi) chart.xAxis.title.text='Номер платежа';
  chart.yAxis.title.text='RUB';
  for(const [i,series] of chart.series.items.entries()) {
    series.fill=palette[i%palette.length];
    if(multi) series.line={fill:palette[i%palette.length],style:'solid',width:2};
  }
}
const interestChart=summary.charts.add('bar',[summary.getRange('B6:B8'),summary.getRange('E6:E8')]);
chartStyle(interestChart,'Всего процентов','M5','W24');
const balanceChart=summary.charts.add('line',summary.getRange(`B${helpersStart}:E${helpersLast}`));
chartStyle(balanceChart,'Остаток долга','M27','W46',true);
const paymentChart=summary.charts.add('line',summary.getRange(`J${helpersStart}:M${helpersStart+n-1}`));
chartStyle(paymentChart,'Платежи до последнего','M49','W68',true);

wb.recalculate();
let comparisons=0;
function near(actual,expected,label,tolerance) {
  comparisons++;
  if(typeof actual!=='number' || !Number.isFinite(actual) || Math.abs(actual-Number(expected))>tolerance) {
    throw new Error(`${label}: got ${actual}, expected ${expected}, tolerance ${tolerance}`);
  }
}
const keys=['payment_count','total_payments','total_interest','total_principal','pv_future_cashflows','pv_minus_outstanding','first_payment','last_payment','closing_balance'];
function checkSummaries(results,label) {
  const values=summary.getRange('C6:K8').values;
  for(let i=0;i<3;i++) {
    if(results[kinds[i]].unsupported) {
      if(typeof values[i][4]==='number') throw new Error(`${label}:${kinds[i]}: unsupported schedule produced numeric PV`);
      continue;
    }
    for(let j=0;j<keys.length;j++) near(values[i][j],results[kinds[i]].summary[keys[j]],`${label}:${kinds[i]}:${keys[j]}`,keys[j]==='payment_count'?0:0.000001);
  }
}
const fieldMap=['number','date','days','opening_balance','interest','principal','payment','closing_balance','credit_period_rate',null,'discount_period_rate','discount_factor','pv_interest','pv_principal','pv_payment'];
for(const k of kinds) {
  const values=sheets[k].getRange(`A${first}:O${last}`).values;
  for(let i=0;i<n;i++) for(let j=0;j<fieldMap.length;j++) {
    const key=fieldMap[j]; if(key===null) continue;
    const expected=key==='date'?serialDate(payload.results[k].payments[i][key]):payload.results[k].payments[i][key];
    const tol=key.startsWith('pv_')?1e-7:['credit_period_rate','discount_period_rate','discount_factor'].includes(key)?1e-12:['number','date','days'].includes(key)?0:1e-6;
    near(values[i][j],expected,`${k}:${i+1}:${key}`,tol);
  }
}
checkSummaries(payload.results,'base');
const scenarioResults=[];
for(const fixture of cases) {
  if(fixture.status==='not_applicable') {
    scenarioResults.push({name:fixture.name,status:'not_applicable',reason:fixture.reason});
    continue;
  }
  const original=params.getRange(fixture.cell).values[0][0];
  params.getRange(fixture.cell).values=[[fixture.value]];
  wb.recalculate();
  checkSummaries(fixture.results,fixture.name);
  for(const k of kinds) {
    if(fixture.results[k].unsupported) continue;
    const top=sheets[k].getRange(`E${first}:H${first}`).values[0];
    const bottom=sheets[k].getRange(`E${last}:H${last}`).values[0];
    for(let j=0;j<4;j++) {
      const key=['interest','principal','payment','closing_balance'][j];
      near(top[j],fixture.results[k].first[key],`${fixture.name}:${k}:first:${key}`,1e-6);
      near(bottom[j],fixture.results[k].last[key],`${fixture.name}:${k}:last:${key}`,1e-6);
    }
  }
  scenarioResults.push({name:fixture.name,passed:true});
  params.getRange(fixture.cell).values=[[original]];
}
// Invalid rates must expose an unavailable result, never silently produce zero.
params.getRange('C8').values=[[-0.01]];
wb.recalculate();
const invalidPv=summary.getRange('G6').values[0][0];
if(typeof invalidPv==='number') throw new Error('Negative discount rate produced a numeric success');
params.getRange('C8').values=[[Number(disc.annual_rate)]];
wb.recalculate();
checkSummaries(payload.results,'restored');
const errorScan=await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#N/A|#NUM!|#NULL!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:100},maxChars:2500});
const errorRecords=errorScan.ndjson.trim().split('\n').filter(Boolean).map(line=>JSON.parse(line));
if(errorRecords.some(r=>r.kind==='match')) throw new Error(`Formula errors: ${errorScan.ndjson}`);

await fs.mkdir(path.join(out,'figures'),{recursive:true});
await fs.mkdir(path.join(out,'qa'),{recursive:true});
const spreadsheet=await SpreadsheetFile.exportXlsx(wb);
await spreadsheet.save(path.join(out,'sprint1_calculations.xlsx'));
// Windows native rendering can abort during shutdown. Isolate it from the
// successful XLSX calculation/export; accept that one status only after an
// explicit completed-render checkpoint and independently readable PNG files.
const rendered = await new Promise((resolve,reject) => {
  const worker=fork(path.join(root,'scripts','render_workbook.mjs'),[out],{stdio:['ignore','pipe','pipe','ipc']});
  let checkpoint=null, stderr='';
  worker.on('message',message=>{checkpoint=message;});
  worker.stderr.on('data',chunk=>{stderr+=chunk;});
  worker.on('error',reject);
  worker.on('exit',(code,signal)=>{
    const knownShutdownAbort=process.platform==='win32' && (code>>>0)===0xC0000409;
    if(checkpoint?.completed===true && (code===0 || knownShutdownAbort)) resolve({files:checkpoint.files,exit_code:code,native_shutdown_abort:knownShutdownAbort});
    else reject(new Error(`Renderer failed: code=${code}, signal=${signal}, ${stderr}`));
  });
});
for(const file of rendered.files) {
  const bytes=await fs.readFile(path.join(out,file));
  if(bytes.length<100 || bytes.subarray(0,8).toString('hex')!=='89504e470d0a1a0a') throw new Error(`Invalid rendered PNG ${file}`);
}
const verification={passed:true,comparisons,scenarios:scenarioResults,negative_rate_rejected:true,
  engine:'Artifact Tool authoring engine; native Excel application not automated',
  error_scan:errorRecords,chart_count:summary.charts.items.length,
  rendering:rendered,payment_count:n,pv_tolerance:'0.00000010',money_tolerance:'0.000001',sources:payload.sources};
await fs.writeFile(path.join(out,'workbook_verification.json'),JSON.stringify(verification,null,2)+'\n');
console.log(JSON.stringify({file:path.join(out,'sprint1_calculations.xlsx'),comparisons,scenarios:scenarioResults,charts:verification.chart_count,passed:true},null,2));
