"""Measured RPC latency and response bytes, not a model/token-cost benchmark."""
import json,statistics,time,platform
from agent_io import AgentIO
rounds=25
text=''.join(f'int function_{i}(int value) {{ return value + {i}; }}\n' for i in range(100))
with AgentIO(native=True) as a:
    path='build/native-benchmark/corpus.cpp';a.write(path,text,'Create reproducible benchmark corpus')
    def measure(method,params):
        times=[];sizes=[]
        for _ in range(rounds):
            t=time.perf_counter();reply=a.call(method,params);times.append((time.perf_counter()-t)*1000);sizes.append(len(json.dumps(reply,ensure_ascii=False,separators=(',',':')).encode('utf-8')))
        return dict(rounds=rounds,median_ms=statistics.median(times),p95_ms=sorted(times)[int(.95*(rounds-1))],response_bytes=statistics.median(sizes))
    cold=time.perf_counter();reply=a.call('outline',{'target':path});cold=(time.perf_counter()-cold)*1000
    assert len(reply['symbols'])==100
    full=measure('view',{'target':path});selected=measure('view',{'target':path,'symbol':'function_50'});outline=measure('outline',{'target':path})
    samples=[]
    offset=text.index('value + 50')+len('value + ')
    for i in range(rounds):
        started=time.perf_counter()
        a.call('edit',{'ops':[{'path':path,'start':offset,'end':offset+2,'text':'51' if i%2==0 else '50'}],'label':'Measured incremental edit'})
        result=a.call('outline',{'target':path});assert len(result['symbols'])==100
        samples.append((time.perf_counter()-started)*1000)
    incremental=dict(rounds=rounds,median_ms=statistics.median(samples),p95_ms=sorted(samples)[int(.95*(rounds-1))])
    report=dict(incremental_edit_outline=incremental,platform=platform.platform(),corpus_bytes=len(text.encode()),symbols=100,cold_outline_ms=cold,full_view=full,symbol_view=selected,warm_outline=outline,response_reduction=full['response_bytes']/selected['response_bytes'],notes=['Single process, sequential RPC, local filesystem; wall-clock timings include Python JSON transport.','No model requests, tokenizer estimates, API cost or comparison against another agent.'])
    a.write('docs/benchmarks/native-'+('windows' if platform.system()=='Windows' else 'linux')+'.json',json.dumps(report,indent=2),'Record actual latency and response-size benchmark')
    print(json.dumps(report,indent=2))
