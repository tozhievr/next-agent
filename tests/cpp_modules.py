"""C++ module suffixes must resolve, parse and participate in workspace symbols."""
import argparse,sys,tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent.parent/'tools'))
from agent_io import AgentIO
p=argparse.ArgumentParser();p.add_argument('executable');args=p.parse_args()
extensions=['cppm','ixx','ccm','cxxm','c++m','mpp']
with tempfile.TemporaryDirectory(prefix='nextagent-cpp-modules-') as temp:
    with AgentIO(Path(temp)/'workspace',native=True,executable=Path(args.executable).resolve()) as a:
        modules=a.call('language_modules')['modules']
        cpp=next(m for m in modules if m['name']=='cpp')
        assert all('.'+ext in cpp['extensions'] for ext in extensions),cpp
        for index,ext in enumerate(extensions+['CPPM','IXX']):
            path=f'unit{index}.{ext}'
            name=f'module_probe_{index}'
            # Interface, partition, implementation and imports use real module syntax.
            header=['export module demo;','export module demo:part;','module demo;','export module demo;\nimport :part;'][index%4]
            text=header+f'\nint {name}(int value) {{ return value + 1; }}\n'
            a.write(path,text,'Create C++20 module regression fixture')
            report=a.call('analyze',{'target':path})
            assert report['language']=='cpp',report
            assert not report['diagnostics'],report
            outline=a.call('outline',{'target':path})
            assert any(s['name']==name for s in outline['symbols']),outline
            selected=a.call('view',{'target':path,'symbol':name,'with_anchor':True})
            assert 'return value + 1' in selected['text'] and selected['anchor'].startswith('ast_'),selected
            found=a.call('find_symbols',{'query':name})
            assert any(s['name']==name for s in found['symbols']),found
print('C++20 module suffixes: native parsing, module syntax, outlines, AST anchors and workspace discovery passed')
