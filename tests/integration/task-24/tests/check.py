from html.parser import HTMLParser
from pathlib import Path
class P(HTMLParser):
 def __init__(self):super().__init__();self.tags=[]
 def handle_starttag(self,t,a):self.tags.append((t,dict(a)))
p=P();p.feed(Path('solution.html').read_text());tags=p.tags
assert any(t=='form' and a.get('id')=='signup' for t,a in tags)
for name,typ in [('name','text'),('email','email'),('password','password')]:
 assert any(t=='input' and a.get('id')==name and a.get('type')==typ and 'required' in a for t,a in tags)
 assert any(t=='label' and a.get('for')==name for t,a in tags)
assert any(t=='button' and a.get('type')=='submit' for t,a in tags)
