from pathlib import Path
s=Path('solution.vert').read_text();assert '#version 450' in s;assert 'gl_Position' in s;assert 'inverse' in s and 'transpose' in s and 'normalize' in s;assert all(x in s for x in ('model','view','projection'))
