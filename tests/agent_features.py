"""End-to-end native agent workflows; all fixture text is edited via NextAgent."""
import argparse,json,sys,tempfile,time,subprocess,os
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'tools'))
from agent_io import AgentIO
p=argparse.ArgumentParser();p.add_argument('executable');args=p.parse_args();exe=str(Path(args.executable).resolve())
def failure(agent,method,params,word):
    try: agent.call(method,params)
    except RuntimeError as error: assert word in str(error),(word,error)
    else: raise AssertionError('Expected '+word)
with tempfile.TemporaryDirectory(prefix='nextagent-agent-') as temp:
    root=Path(temp)/'workspace'
    original='# Unicode: caf\u00e9 \U0001f600\n'+'def login(user):\n    return user\n\ndef logout(user):\n    return None\n'
    with AgentIO(root,native=True,executable=exe) as a:
        a.write('auth.py',original)
        if os.name=='nt':
            a.write('Case.py','original\n');a.write('case.py','updated\n');assert a.read('Case.py')=='updated\n'
            a.write('\u0424\u0430\u0439\u043b.py','first\n');a.write('\u0444\u0430\u0439\u043b.py','second\n');assert a.read('\u0424\u0430\u0439\u043b.py')=='second\n'
        else:
            a.write('Case.py','upper\n');a.write('case.py','lower\n');assert a.read('Case.py')=='upper\n' and a.read('case.py')=='lower\n'


        query=a.call('labels',{'target':'auth.py','node_kind':'function_def','symbol':'login'})
        assert len(query['nodes'])==1,query
        anchor=query['nodes'][0]['anchor']
        a.call('labels',{'action':'bind','label':'auth.login','anchor':anchor})
        assert 'return user' in a.call('view',{'label':'auth.login'})['text']
        read=a.call('view',{'target':'auth.py','symbol':'login','with_anchor':True});assert read['anchor']==anchor and 'return user' in read['text']
        failure(a,'labels',{'action':'bind','label':'auth.login','anchor':anchor},'label_exists')
        failure(a,'labels',{'action':'bind','label':anchor,'anchor':anchor},'Reserved')
        plan=a.call('prepare',{'edits':[{'label':'auth.login','new':'def login(user):\n    return user.strip()\n\n'}],'files':[{'op':'create','path':'tests/test_auth.py','text':'import unittest\nclass TestAuth(unittest.TestCase):\n    def test_true(self): self.assertTrue(True)\n'}]})
        assert not plan['applied'] and a.read('auth.py')==original
        proposed=a.call('read_batch',{'requests':[{'plan':plan['plan'],'target':'auth.py','side':'after'}]});assert 'strip' in proposed['items'][0]['text'] and 'strip' not in a.read('auth.py')
        assert all('start' in x for x in plan['diff'])
        # Unrelated text before the labeled node shifts its range but preserves identity.
        a.write('auth.py','# inserted prefix\n'+original)
        resolved=a.call('view',{'label':'auth.login'});assert resolved['start']>query['nodes'][0]['start']
        failure(a,'commit',{'plan':plan['plan']},'stale_plan')
        plan=a.call('prepare',{'edits':[{'label':'auth.login','new':'def login(user):\n    return user.strip()\n\n'}],'files':[{'op':'create','path':'tests/test_auth.py','text':'import unittest\nclass TestAuth(unittest.TestCase):\n    def test_true(self): self.assertTrue(True)\n'}]})
        committed=a.call('commit',{'plan':plan['plan']});assert committed['written']
        retry=a.call('commit',{'plan':plan['plan']});assert retry['already_committed'] and retry['rev']==committed['rev']
        failure(a,'view',{'label':'auth.login'},'stale_anchor')
        a.call('history',{'op':'undo'})
        assert 'strip' not in a.read('auth.py') and not any(x['path']=='tests' for x in a.tree('',False))
        assert 'return user' in a.call('view',{'label':'auth.login'})['text']
        a.call('history',{'op':'redo'});assert 'strip' in a.read('auth.py')
        batch=a.call('read_batch',{'requests':[{'target':'auth.py'},{'target':'tests/test_auth.py'},{'target':'missing.py'}],'budget_bytes':512})
        assert len(json.dumps(batch,ensure_ascii=False,separators=(',',':')).encode())<=512
        a.write('large.txt','\U0001f600'*1000)
        bounded=a.call('read_batch',{'requests':[{'target':'large.txt'}],'budget_bytes':512})
        assert bounded['truncated'] and len(json.dumps(bounded,ensure_ascii=False,separators=(',',':')).encode())<=512
        # Recursive move is one revision, including parent creation and deletion.
        moved=a.call('change',{'files':[{'op':'move','path':'tests','to':'checks','recursive':True}]});assert moved['written']
        assert any(x['path']=='checks/test_auth.py' for x in a.tree())
        a.call('history',{'op':'undo'});assert any(x['path']=='tests/test_auth.py' for x in a.tree())
        a.call('history',{'op':'redo'});assert any(x['path']=='checks/test_auth.py' for x in a.tree())
        failure(a,'prepare',{'files':[{'op':'move','path':'checks','to':'checks/sub','recursive':True}]},'Invalid move')
        failure(a,'prepare',{'files':[{'op':'create','path':'../escape.py','text':'x'}]},'escapes')
        failure(a,'prepare',{'files':[{'op':'create','path':'.git/config','text':'x'}]},'reserved')
        failure(a,'prepare',{'files':[{'op':'delete','path':'checks'}]},'recursive')
        sequence=a.call('change',{'files':[{'op':'mkdir','path':'staged'},{'op':'create','path':'staged/new.py','text':'x=1\n'},{'op':'move','path':'staged','to':'moved','recursive':True}]})
        assert sequence['written'] and a.read('moved/new.py')=='x=1\n'
        a.call('history',{'op':'undo'});assert not any(x['path'] in ('staged','moved') for x in a.tree('',False))
        a.write('ambiguous.py','def repeated(x):\n    return x+x\n')
        raw=a.call('labels',{'target':'ambiguous.py','node_kind':'name','symbol':'repeated'})
        ids=[n['anchor'] for n in raw['nodes']];duplicate=next((x for x in ids if ids.count(x)>1),None)
        assert duplicate,raw
        failure(a,'view',{'anchor':duplicate},'ambiguous_anchor')

        a.call('change',{'files':[{'op':'move','path':'checks','to':'tests','recursive':True}]})
        check=a.call('run_check',{'kind':'python','inputs':['auth.py','tests/test_auth.py']});assert check['exit_code']==0 and check['fresh'],check
        assert a.call('check_status',{'check_id':check['check_id']})['fresh']
        a.write('auth.py',a.read('auth.py')+'# changed after checks\n')
        assert not a.call('check_status',{'check_id':check['check_id']})['fresh']
        # External writer uses a different NextAgent root/journal to simulate a formatter.
        with AgentIO(temp,native=False,executable=exe) as external:
            text=a.read('auth.py')
            external.write('workspace/auth.py','# external formatter\n'+text)
        assert a.call('view',{'target':'auth.py'})['text'].startswith('# external formatter')
        query=a.call('labels',{'target':'auth.py','node_kind':'function_def','symbol':'logout'})
        a.call('labels',{'action':'bind','label':'auth.logout','anchor':query['nodes'][0]['anchor']})
        capabilities=a.call('capabilities');assert len(capabilities['modules'])==30
        assert all(not m['capabilities']['semantic_rename'] for m in capabilities['modules'])
        for ext,text in [('ts','export function probe(x: number): number { return x+1; }\n'),('js','function probe(x) { return x+1; }\n'),('kt','fun probe(x: Int): Int { return x+1 }\n'),('swift','func probe(_ x: Int) -> Int { return x+1 }\n'),('go','package main\nfunc probe(x int) int { return x+1 }\n')]:
            a.write('probe.'+ext,text)
            symbol=a.call('view',{'target':'probe.'+ext,'symbol':'probe','with_anchor':True})
            assert 'return' in symbol['text'] and len(symbol['text'])>20,(ext,symbol)
        large=''.join(f'def large_{i}(x):\n    return x+{i}\n\n' for i in range(900))
        a.write('many.py',large)
        one=a.call('view',{'target':'many.py','symbol':'large_899','with_anchor':True})
        assert 'return x+899' in one['text'],one
        filtered=a.call('labels',{'target':'many.py','node_kind':'function_def','limit':1})
        assert filtered['matched']==900 and filtered['truncated'],filtered
        a.call('warm',{'targets':['auth.py']})
    # Named anchors and immutable plans survive process restart; derived caches avoid reparse.
    with AgentIO(root,native=True,executable=exe) as a:
        assert 'return None' in a.call('view',{'label':'auth.logout'})['text']
        assert a.call('prepare',{'plan':plan['plan']})['plan']==plan['plan']
        assert any(m['cache_hits']>0 for m in a.call('capabilities')['modules'])
        failure(a,'view',{'label':'unknown'},'unknown_anchor')
    with AgentIO(root,native=True,executable=exe,protocol='mcp') as mcp:
        initialized=mcp.call('initialize',{'protocolVersion':'2024-11-05','capabilities':{},'clientInfo':{'name':'agent-features','version':'1'}})
        tools=mcp.call('tools/list');assert len(tools['tools'])==32
        result=mcp.call('tools/call',{'name':'view','arguments':{'label':'auth.logout'}})
        assert not result.get('isError',False) and 'return None' in result['content'][0]['text'],result

    # Independent sessions race to claim one label; journal CAS permits exactly one winner.
    from concurrent.futures import ThreadPoolExecutor
    with AgentIO(root,native=True,executable=exe) as first, AgentIO(root,native=True,executable=exe) as second:
        left=first.call('view',{'target':'auth.py','symbol':'logout','with_anchor':True})['anchor']
        right=second.call('view',{'target':'ambiguous.py','symbol':'repeated','with_anchor':True})['anchor']
        def bind(pair):
            agent,token=pair
            try:agent.call('labels',{'action':'bind','label':'race.owner','anchor':token});return True
            except RuntimeError as error:assert 'label_exists' in str(error) or 'metadata_conflict' in str(error);return False
        with ThreadPoolExecutor(max_workers=2) as pool:assert sum(pool.map(bind,[(first,left),(second,right)]))==1
    # Exercise the legacy CLI flush path with more than 64 paths as well as the new API.
    edits=[{'path':f'bulk/{i}.txt','new':str(i),'create':True} for i in range(70)]
    subprocess.run([exe,'--root',str(root),'patch','-'],input=json.dumps({'edits':edits}),text=True,encoding='utf-8',capture_output=True,check=True)
    with AgentIO(root,native=True,executable=exe) as verified:assert len(verified.tree('bulk'))==70
    cli=subprocess.run([exe,'--root',str(root),'call','analyze','-'],input=json.dumps({'target':'auth.py'}),text=True,encoding='utf-8',capture_output=True,check=True)
    assert not json.loads(cli.stdout)['diagnostics'],cli.stdout


print('AST labels, persistence, stale anchors/plans, atomic file operations, undo/redo, bounded reads, cache and check freshness passed')
