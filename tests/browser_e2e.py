from pathlib import Path
import subprocess,tempfile,urllib.request,time,json,socket,base64,os,struct
r=Path(__file__).resolve().parent.parent
profile=Path(tempfile.mkdtemp(prefix='portfolio-edge-test-'))
app=subprocess.Popen([str(r/'build/Release/portfolio_app.exe'),'--no-browser'],cwd=r)
edge=None;ws=None
try:
 for i in range(100):
  if app.poll() is not None:raise RuntimeError('App exited')
  try:
   urllib.request.urlopen('http://127.0.0.1:8765/health',timeout=1).close();break
  except OSError:time.sleep(.1)
 edge=subprocess.Popen([r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe','--headless=new','--disable-gpu','--no-first-run','--no-default-browser-check','--remote-debugging-port=0','--remote-allow-origins=http://localhost','--user-data-dir='+str(profile),'about:blank'],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 portfile=profile/'DevToolsActivePort'
 for i in range(200):
  if portfile.exists():break
  time.sleep(.1)
 port=int(portfile.read_text().splitlines()[0])
 targets=json.load(urllib.request.urlopen(f'http://127.0.0.1:{port}/json'))
 target=next(x for x in targets if x['type']=='page')
 from urllib.parse import urlparse
 u=urlparse(target['webSocketDebuggerUrl']);ws=socket.create_connection((u.hostname,u.port),timeout=120)
 key=base64.b64encode(os.urandom(16)).decode()
 ws.sendall((f'GET {u.path} HTTP/1.1\r\nHost: {u.netloc}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\nOrigin: http://localhost\r\n\r\n').encode())
 header=b''
 while b'\r\n\r\n' not in header:header+=ws.recv(1)
 assert b'101' in header
 def exact(n):
  data=b''
  while len(data)<n:
   part=ws.recv(n-len(data))
   if not part:raise RuntimeError('Websocket closed')
   data+=part
  return data
 def frame(op,data):
  mask=os.urandom(4);n=len(data);h=bytes([128|op])
  h+=bytes([128|n]) if n<126 else bytes([128|126])+struct.pack('!H',n) if n<65536 else bytes([128|127])+struct.pack('!Q',n)
  ws.sendall(h+mask+bytes(c^mask[i%4] for i,c in enumerate(data)))
 seq=0
 def cdp(method,params=None):
  global seq
  seq+=1;current=seq;frame(1,json.dumps({'id':current,'method':method,'params':params or {}}).encode());parts=b''
  while True:
   a,b=exact(2);n=b&127
   if n==126:n=struct.unpack('!H',exact(2))[0]
   elif n==127:n=struct.unpack('!Q',exact(8))[0]
   mask=exact(4) if b&128 else None;data=exact(n)
   if mask:data=bytes(c^mask[i%4] for i,c in enumerate(data))
   op=a&15
   if op==9:frame(10,data);continue
   if op==8:raise RuntimeError('Browser disconnected')
   if op in (0,1):parts+=data
   if not a&128:continue
   if op not in (0,1):continue
   message=json.loads(parts);parts=b''
   if message.get('id')==current:
    if 'error' in message:raise RuntimeError(message['error'])
    return message.get('result',{})
 def evaluate(expr):
  result=cdp('Runtime.evaluate',{'expression':expr,'awaitPromise':True,'returnByValue':True})
  if 'exceptionDetails' in result:raise RuntimeError(json.dumps(result['exceptionDetails'],ensure_ascii=False))
  return result.get('result',{}).get('value')
 cdp('Page.enable');cdp('Page.navigate',{'url':'http://127.0.0.1:8765/'})
 for i in range(200):
  if evaluate("document.readyState==='complete' && !!document.getElementById('count')"):break
  time.sleep(.1)
 flows=(r/'tests/fixtures/cf/cashflows.csv').read_text(encoding='utf-8-sig');summary=(r/'tests/fixtures/cf/deal_summary.csv').read_text(encoding='utf-8-sig')
 js=r"""(async()=>{
 const assert=(b,s)=>{if(!b)throw Error(s)},wait=async(test)=>{for(let i=0;i<400;i++){if(test())return;await new Promise(r=>setTimeout(r,100))}throw Error('UI wait timeout: '+document.getElementById('notice').textContent)};
 el('preset').value='short';el('preset').dispatchEvent(new Event('change'));assert(el('min_term').value==='1'&&el('max_term').value==='12','Preset failed');el('count').value='17';el('profileName').value='Browser E2E';await el('saveProfile').onclick();assert(JSON.parse(localStorage.getItem('portfolio-profiles-v1'))['Browser E2E'].count==='17','Profile save failed');
 el('settings').requestSubmit();await wait(()=>last&&last.count==='17');await wait(()=>el('generationFrame').contentDocument?.querySelectorAll('#rows tr').length===17);const first=el('generationFrame').getAttribute('src');const csv=await (await fetch(first.replace('report.html','portfolio.csv'))).text();assert(csv.trim().split('\n').length===18,'Wrong CSV count');
 await el('repeat').onclick();const second=el('generationFrame').getAttribute('src');assert(first!==second,'Replay did not run');assert(csv===await (await fetch(second.replace('report.html','portfolio.csv'))).text(),'Replay CSV mismatch');await wait(()=>el('generationFrame').contentDocument?.querySelector('.bar'));el('generationFrame').contentDocument.querySelector('.bar').click();assert(el('generationFrame').contentDocument.getElementById('filter').textContent!=='Все сделки','Chart drilldown failed');
 const file=(id,name,text)=>{const dt=new DataTransfer();dt.items.add(new File([text],name,{type:'text/csv'}));el(id).files=dt.files};
 file('summaryFile','deal_summary.csv',SUMMARY);file('flowsFile','cashflows.csv',FLOWS);show('payments');await el('importCF').onclick();assert(el('notice').textContent.includes('проверены и загружены'),'CF UI import failed');await wait(()=>el('paymentsFrame').contentDocument?.querySelector('#rows tr'));
 const doc=el('paymentsFrame').contentDocument;doc.getElementById('deal').value='1';doc.getElementById('from').value='2022-09-22';doc.getElementById('to').value='2022-09-22';doc.getElementById('from').dispatchEvent(new Event('change'));const row=doc.querySelector('#rows tr');assert(row&&row.children[3].textContent.replace(/\s/g,'')==='26037,32','Control payment UI failed');
 show('factors');const ff=document.querySelector('#factors iframe');await wait(()=>ff.contentDocument?.querySelector('#bars .bar'));assert(ff.contentDocument.getElementById('npv').textContent.includes('OK'),'Demo balance failed');ff.contentDocument.querySelector('#bars .bar').click();assert(ff.contentDocument.getElementById('factorDetail').textContent.includes('Сдвиг'),'Factor detail failed');
 show('generator');el('preset').value='custom:Browser E2E';el('preset').dispatchEvent(new Event('change'));assert(el('count').value==='17','Saved profile load failed');el('ann').value='1';assert((()=>{try{config();return false}catch{return true}})(),'Client validation failed');el('ann').value='60';return 'BROWSER E2E PASSED: presets, profiles, submit, replay, drilldown, CF upload, 26037.32, demo details, validation';
 })()""".replace('SUMMARY',json.dumps(summary,ensure_ascii=False)).replace('FLOWS',json.dumps(flows,ensure_ascii=False))
 print(evaluate(js),flush=True)
 artifacts=r/'test-results';artifacts.mkdir(exist_ok=True)
 shot=cdp('Page.captureScreenshot',{'format':'png','captureBeyondViewport':False})
 (artifacts/'app-ui.png').write_bytes(base64.b64decode(shot['data']))
 (artifacts/'browser-e2e.txt').write_text('PASS: real Edge UI tests\n',encoding='utf-8')
 cdp('Browser.close')
finally:
 if ws:ws.close()
 if edge:
  try:edge.wait(timeout=10)
  except subprocess.TimeoutExpired:edge.terminate()
 app.terminate();app.wait(timeout=10)
