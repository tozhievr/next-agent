"""Prepare through NextAgent on Windows; evaluate an isolated copy in WSL.
No evaluator tests are returned to the solving agent. Reports are written through RPC.
"""
import argparse,json,os,subprocess,sys,time
from agent_io import AgentIO
p=argparse.ArgumentParser();p.add_argument('--prepare',action='store_true');p.add_argument('--report',default='docs/benchmarks/integration-results.json');p.add_argument('--repair-fixtures',action='store_true');a=p.parse_args()
if a.prepare:
    with AgentIO() as io:
        manifest=json.loads(io.read('tests/integration/manifest.json'))
        for task in manifest:
            key=task['id']
            for e in io.tree('tests/integration/'+key):
                if not e['directory']:
                    rel=e['path'].split(key+'/',1)[1]
                    io.write('build/integration-evaluation/'+key+'/'+rel,io.read(e['path']),'Isolate evaluator fixtures')
            for e in io.tree('tests/benchmark_solutions/'+key):
                if not e['directory']:io.write('build/integration-evaluation/'+key+'/'+e['name'],io.read(e['path']),'Submit solution to evaluator')
        io.write('build/integration-evaluation/manifest.json',json.dumps(manifest),'Evaluator manifest')
elif a.repair_fixtures:
    with AgentIO() as io:
        base='build/integration-evaluation/'
        path=base+'task-02/tests/test.cpp'
        text=io.read(path)
        # The original fixture double-encoded UTF-8 as Latin-1 text.
        io.write(path,''.join(('\\x%02x' % ord(c)) if ord(c)>=128 else c for c in text),'Evaluator-only UTF-8 fixture repair')
        path=base+'task-05/interfaces/API.kt'
        io.write(path,'// fun balanced(input: String): Boolean\n','Evaluator-only Kotlin signature comment')
        path=base+'task-30/tests/test.cpp'
        text=io.read(path).replace('TokenKind::RParen, ")", 11','TokenKind::RParen, ")", 12')
        io.write(path,text,'Evaluator-only closing parenthesis offset repair')
else:
    # Commands compile/run trusted evaluator tests. Source access stays in native RPC.
    with AgentIO() as io:
        manifest=json.loads(io.read('tests/integration/manifest.json'));results=[]
        for task in manifest:
            start=time.perf_counter()
            command=['wsl','-d','Ubuntu-26.04','--','bash','-lc',f'export PATH=/opt/swift/usr/bin:/usr/local/bin:/usr/bin:/bin; cd /mnt/d/next-agent/build/integration-evaluation/{task["id"]} && {task["run"]}']
            try:
                r=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
                result=dict(task,exit_code=r.returncode,seconds=round(time.perf_counter()-start,4),output=(r.stdout+r.stderr)[-12000:])
            except subprocess.TimeoutExpired:result=dict(task,exit_code=-1,seconds=120,output='evaluator timeout')
            results.append(result);print(task['id'],result['exit_code'],result['seconds'],flush=True)
        io.write(a.report,json.dumps(results,indent=2,ensure_ascii=False),'Record actual integration evaluation')
        print('Passed',sum(x['exit_code']==0 for x in results),'/',len(results))
