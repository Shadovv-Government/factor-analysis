import fs from 'node:fs/promises';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const require=createRequire(path.join(root,'.runtime','package.json'));
const {FileBlob,SpreadsheetFile}=await import(pathToFileURL(require.resolve('@oai/artifact-tool')).href);
const sharp=require('sharp');
const out=path.resolve(process.argv[2]);
const data=JSON.parse(await fs.readFile(path.join(out,'calculation.json'),'utf8'));
const last=6+data.results.ANN.payments.length;
const wb=await SpreadsheetFile.importXlsx(await FileBlob.load(path.join(out,'sprint1_calculations.xlsx')));
const files=[];

async function render(file,sheetName,range,scale=1.5,cropHeaders=false) {
  const blob=await wb.render({sheetName,range,scale,format:'png'});
  let bytes=Buffer.from(await blob.arrayBuffer());
  if(cropHeaders) {
    // Remove only the workbook row/column headers from this chart export.
    const meta=await sharp(bytes).metadata();
    bytes=await sharp(bytes).extract({left:80,top:40,width:meta.width-80,height:meta.height-40}).png().toBuffer();
  }
  await fs.writeFile(path.join(out,file),bytes);
  files.push(file);
}
await render('qa/comparison.png','Сравнение','B2:K26');
await render('qa/parameters.png','Параметры','B2:H22');
for(const k of ['ANN','SIMPLE','RGM']) {
  await render(`qa/${k}_first.png`,k,'A2:O12');
  await render(`qa/${k}_last.png`,k,`A${last-3}:O${last}`);
}
await render('figures/interest_totals.png','Сравнение','M5:W25',2,true);
await render('figures/balances.png','Сравнение','M27:W48',2,true);
await render('figures/regular_payments.png','Сравнение','M49:W70',2,true);
if(process.send) await new Promise((resolve,reject)=>process.send({completed:true,files},error=>error?reject(error):resolve()));
