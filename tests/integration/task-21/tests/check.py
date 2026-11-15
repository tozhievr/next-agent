from pathlib import Path
s=Path('solution.frag').read_text();assert '#version 450' in s;assert 'vColor' in s and 'outColor' in s;assert '0.2126' in s and '0.7152' in s and '0.0722' in s
