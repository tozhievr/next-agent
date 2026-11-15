from pathlib import Path
import re
s=re.sub(r'/\*.*?\*/','',Path('solution.css').read_text(),flags=re.S).lower();assert re.search(r'\.cards\s*\{[^}]*display\s*:\s*grid\s*;',s,re.S);assert re.search(r'grid-template-columns\s*:\s*repeat\s*\(\s*auto-fit\s*,\s*minmax\s*\(\s*240px\s*,\s*1fr\s*\)\s*\)',s)
