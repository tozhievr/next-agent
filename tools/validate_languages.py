"""Exercise each native module through NextAgent RPC; persist honest per-module results."""
import argparse,json,sys,time
from agent_io import AgentIO,ROOT
import os
samples={
'antlrv4_lx':('g4','grammar Demo; start: ID EOF; ID: [a-z]+; WS: [ \\t\\r\\n]+ -> skip;\n'),
'c':('c','// caf\u00e9 \U0001f600\r\ntypedef struct Point { int x; int y; } Point;\nint probe(int x){return x+1;}\n'),
'cpp':('cpp','// caf\u00e9 \U0001f600\r\nstruct Point { int x; };\nint probe(int x){return x+1;}\n'),
'csharp':('cs','class Demo { public int Probe(int x) { return x+1; } }\n'),
'css3':('css','.probe { color: red; }\n'),
'ecmascript':('es','function probe(x) { return x+1; }\n'),
'expr':('expr','probe = 1+2;\n'),
'glsl':('frag','#version 450\nlayout(location=0) out vec4 color;\nvoid main(){color=vec4(1.0);}\n'),
'go':('go','package main\nfunc probe(x int) int { return x+1 }\n'),
'haskell':('hs','module Demo where\nprobe x = x + 1\n'),
'html5':('html','<!doctype html><html><head><title>Demo</title></head><body><p id="probe">Hello</p></body></html>\n'),
'indent':('indent','def probe(x):\n    return x\n'),
'javascript':('js','function probe(x) { return x+1; }\n'),
'json':('json','{"probe":1,"items":[true,null]}\n'),
'json5':('json5','{probe:1, items:[true,null,],}\n'),
'kotlin':('kt','fun probe(x: Int): Int { return x + 1 }\n'),
'labels':('labels','fn probe(x: Int) -> Int { return x; }\n'),
'lean':('lean','def probe (x : Nat) : Nat := x + 1\n'),
'lua':('lua','function probe(x) return x+1 end\n'),
'mainlang':('mainlang','const probe = 1;\n'),
'mermaid':('mmd','classDiagram\n class Probe\n'),
'nasm':('asm','section .text\nglobal probe\nprobe:\n mov eax, 1\n ret\n'),
'php':('php','<?php function probe($x) { return $x + 1; }\n'),
'preds':('preds','fn probe() { ; }\n'),
'python':('py','# caf\u00e9 \U0001f600\r\ndef probe(x):\n    return x + 1\n'),
'rust':('rs','fn probe(x: i32) -> i32 { x+1 }\n'),
'splitparser':('splitparser','let probe = 1+2;\n'),
'swift':('swift','func probe(_ x: Int) -> Int { return x + 1 }\n'),
'template':('template','<p>${probe}</p>\n'),
'typescript':('ts','export function probe(x: number): number { return x+1; }\n')}
args=argparse.ArgumentParser();args.add_argument('--report',default='docs/benchmarks/language-matrix.json');args.add_argument('--executable');args=args.parse_args()
rows=[]
with AgentIO(ROOT / "build" / "language-validation" / ("windows" if os.name=="nt" else "linux"),native=True,executable=args.executable) as agent:
    registered=agent.call('language_modules')['modules'];assert {x['name'] for x in registered}==set(samples)
    for lang,(ext,text) in samples.items():
        target=f'probe.{ext}';agent.write(target,text,'Native module correctness fixture')
        start=time.perf_counter()
        try:
            report=agent.call('analyze',{'target':target});outline=agent.call('outline',{'target':target,'nested':True})
            assert report['language']==lang,(lang,report)
            diagnostics=report['diagnostics']
            assert all(0<=symbol['start']<=symbol['end']<=len(text) for symbol in outline['symbols']), (lang,outline)
            if lang in ['c','cpp','python']:
                selected=agent.call('view',{'target':target,'symbol':'probe'})
                assert 'probe' in selected['text'] and 'caf' not in selected['text'], (lang,selected)
            row=dict(language=lang,empty_sample=not bool(text),loaded=True,syntax_errors=len([x for x in diagnostics if x.get('severity',1)==1]),diagnostics=diagnostics,symbols=outline['symbols'],seconds=round(time.perf_counter()-start,6),sample=text)
            # A real incremental edit must match fresh-source outline after updating the journal.
            agent.write(target,text+'\n','Incremental whitespace edit')
            after=agent.call('outline',{'target':target,'nested':True})
            assert [(x['name'],x['kind']) for x in after['symbols']]==[(x['name'],x['kind']) for x in outline['symbols']]
            updated=agent.call('analyze',{'target':target});assert updated['version']>report['version'];assert updated['snapshot_version']>report['snapshot_version'];assert updated['diagnostics']==diagnostics
            row['incremental_stable']=True
        except Exception as exc:row=dict(language=lang,loaded=False,error=repr(exc))
        rows.append(row);print(lang,row.get('syntax_errors'),row.get('error',''),flush=True)
    # Diagnostics must catch corruption and recover after restoration, with fresh revisions.
    for lang in ['c','cpp','json','python','typescript']:
        ext,text=samples[lang];target=f'probe.{ext}'
        bad={'c':'int broken( {','cpp':'int broken( {','json':'{"broken": [}','python':'def broken(:\n','typescript':'function broken( {'}[lang]
        agent.write(target,bad,'Negative syntax regression')
        report=agent.call('analyze',{'target':target})
        assert report['language']==lang and any(d['severity']==1 for d in report['diagnostics']), (lang,report)
        agent.write(target,text,'Recover valid syntax fixture')
        repaired=agent.call('analyze',{'target':target})
        assert repaired['version']>report['version'] and not repaired['diagnostics'], (lang,repaired)
with AgentIO(executable=args.executable) as writer:
    writer.write(args.report,json.dumps(rows,ensure_ascii=False,indent=2),'Record native module matrix including failures')
print('Loaded',sum(x['loaded'] for x in rows),'/',len(rows))

assert all(r['loaded'] and r.get('syntax_errors')==0 and r.get('incremental_stable') and not r.get('empty_sample') for r in rows), 'Native module regression; inspect recorded matrix'
