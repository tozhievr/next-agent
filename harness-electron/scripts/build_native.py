"""Build the in-process Node-API bridge using the project's native C++26 core."""
import json,os,subprocess,sys,urllib.request
from pathlib import Path
APP=Path(__file__).resolve().parents[1];ROOT=APP.parent
platform='windows' if os.name=='nt' else 'macos' if sys.platform=='darwin' else 'linux';build=ROOT/'build'/platform
headers=APP/'node_modules/node-api-headers/include'
args=['cmake','-S',str(ROOT),'-B',str(build),'-DNEXTAGENT_ELECTRON=ON',f'-DNODE_API_INCLUDE={headers}']
if os.name=='nt':
 version=json.loads((APP/'node_modules/electron/package.json').read_text())['version']
 library=ROOT/'build/electron-headers'/version/'node.lib';library.parent.mkdir(parents=True,exist_ok=True)
 if not library.exists():urllib.request.urlretrieve(f'https://artifacts.electronjs.org/headers/dist/v{version}/win-x64/node.lib',library)
 args.append(f'-DNODE_API_LIBRARY={library}')
subprocess.run(args,check=True)
subprocess.run(['cmake','--build',str(build),'--target','nextagent_desktop','--parallel','8'],check=True)
