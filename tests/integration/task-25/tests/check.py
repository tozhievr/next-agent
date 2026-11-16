from html.parser import HTMLParser
from pathlib import Path
class P(HTMLParser):
 def __init__(self):super().__init__();self.tags=[];self.text=[]
 def handle_starttag(self,t,a):self.tags.append((t,dict(a)))
 def handle_data(self,d):self.text.append(d.strip())
p=P();p.feed(Path('solution.html').read_text());ts=p.tags
assert any(t=='table' and a.get('id')=='inventory' for t,a in ts)
assert all(any(t==x for t,a in ts) for x in ['caption','thead','tbody'])
assert sum(t=='th' and a.get('scope')=='col' for t,a in ts)>=3
assert sum(t=='th' and a.get('scope')=='row' for t,a in ts)>=3
for x in ['Product','Price','Stock','Pen','Book','Bag']:assert x in p.text
