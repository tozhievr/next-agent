from pathlib import Path
import re
s=re.sub(r'/\*.*?\*/','',Path('solution.css').read_text(),flags=re.S).lower();assert re.search(r'\.action\s*\{[^}]*transition\s*:',s,re.S);assert re.search(r'\.action:focus-visible\s*\{[^}]*outline\s*:',s,re.S);assert re.search(r'@media\s*\(\s*prefers-reduced-motion\s*:\s*reduce\s*\)',s);assert re.search(r'animation\s*:',s) or re.search(r'transition\s*:\s*none',s)
