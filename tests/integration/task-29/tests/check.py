from pathlib import Path
import re
s=re.sub(r'/\*.*?\*/','',Path('solution.css').read_text(),flags=re.S).lower();assert re.search(r'\.product-container\s*\{[^}]*container-type\s*:\s*inline-size',s,re.S);assert '.product-card' in s;assert re.search(r'@container\s*\(\s*min-width\s*:\s*480px\s*\)',s);assert re.search(r'grid-template-columns\s*:',s);assert re.search(r'max-width\s*:\s*100%',s)
