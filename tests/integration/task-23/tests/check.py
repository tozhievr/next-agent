from pathlib import Path
s=Path('solution.frag').read_text();assert '#version 450' in s;assert 'evaluateSpecular' in s;assert 'roughness' in s;assert 'dot(' in s;assert 'pow(' in s;assert 'normalize(' in s
