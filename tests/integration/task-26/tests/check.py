from html.parser import HTMLParser
from pathlib import Path
class P(HTMLParser):
 def __init__(self):super().__init__();self.tags=[]
 def handle_starttag(self,t,a):self.tags.append((t,dict(a)))
p=P();p.feed(Path('solution.html').read_text());t=p.tags
assert any(n=='dialog' and a.get('id')=='confirm' and a.get('aria-labelledby')=='dialog-title' and a.get('aria-describedby')=='dialog-description' for n,a in t)
assert any(n in ('h1','h2','h3') and a.get('id')=='dialog-title' for n,a in t)
assert any(n=='p' and a.get('id')=='dialog-description' for n,a in t)
assert any(n=='form' and a.get('method')=='dialog' for n,a in t)
assert any(n=='button' for n,a in t)
