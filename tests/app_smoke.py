from pathlib import Path
import subprocess,urllib.request,urllib.parse,urllib.error,time,re
r=Path(__file__).resolve().parent.parent
p=subprocess.Popen([str(r/'build/Release/portfolio_app.exe'),'--no-browser'],cwd=r)
base='http://127.0.0.1:8765'
try:
 for i in range(100):
  if p.poll() is not None:raise RuntimeError('Server exited')
  try:
   with urllib.request.urlopen(base+'/health',timeout=1) as response:assert response.read()==b'OK'
   break
  except OSError:time.sleep(.1)
 else:raise RuntimeError('Server startup timeout')
 html=urllib.request.urlopen(base).read().decode()
 token=re.search(r'name="session-token" content="([^"]+)"',html).group(1)
 def post(path,values,auth=token):
  req=urllib.request.Request(base+path,data=urllib.parse.urlencode(values).encode(),headers={'X-Token':auth,'Origin':base})
  with urllib.request.urlopen(req,timeout=60) as response:return response.read().decode()
 config=(r/'profiles/short.ini').read_text()
 assert post('/validate',{'config':config})=='OK'
 urls=[post('/generate',{'config':config}) for _ in range(2)]
 def get(url):return urllib.request.urlopen(base+url,timeout=30).read()
 assert get(urls[0].replace('report.html','portfolio.csv'))==get(urls[1].replace('report.html','portfolio.csv'))
 assert b'2026-08' in get('/factors')
 imported=post('/import',{'summary':(r/'tests/fixtures/cf/deal_summary.csv').read_text(),'flows':(r/'tests/fixtures/cf/cashflows.csv').read_text()})
 assert b'26000' in get(imported)
 for path,values,auth in [('/validate',{'config':config.replace('count=1000','count=0')},token),('/generate',{'config':config},'wrong'),('/import',{'summary':'bad','flows':'bad'},token)]:
  try:post(path,values,auth);raise AssertionError('Invalid request accepted')
  except urllib.error.HTTPError as e:assert e.code==400
 try:get('/run/../CMakeLists.txt');raise AssertionError('Traversal accepted')
 except urllib.error.HTTPError:pass
 print('APP API TESTS PASSED: generation, replay CSV, validation, CF import, demo, token, path protection')
finally:
 p.terminate();p.wait(timeout=10)
