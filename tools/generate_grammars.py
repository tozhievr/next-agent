"""Generate the 30 language plugins from a next-langserver checkout.

The pinned public snapshot stores ANTLR grammars only. ``tools/build_plugins.py``
compiles the C99 that lxgen writes under ``grammars/*/generated*``.

python tools/generate_grammars.py --source next-langserver
"""
import argparse
import os
import subprocess
import sys
from pathlib import Path

# id, grammar under grammars/, output directory under grammars/
LANGUAGES = (
    ("antlrv4_lx", "antlr4-lx/ANTLRv4Parser.g4", "antlr4-lx/generated"),
    ("c", "c/CParser.g4", "c/generated"),
    ("cpp", "cpp/CPP23Parser.g4", "cpp/generated"),
    ("csharp", "csharp/CSharpParser.g4", "csharp/generated"),
    ("css3", "css3/css3Parser.g4", "css3/generated"),
    ("ecmascript", "javascript/ECMAScript.g4", "javascript/generated_ecmascript"),
    ("expr", "expr/Expr.g4", "expr/generated"),
    ("glsl", "glsl/GLSLParser.g4", "glsl/generated"),
    ("go", "go/GoParser.g4", "go/generated"),
    ("haskell", "haskell/HaskellParser.g4", "haskell/generated"),
    ("html5", "html5/HTMLParser.g4", "html5/generated"),
    ("indent", "indent/Indent.g4", "indent/generated"),
    ("javascript", "javascript/JavaScriptParser.g4", "javascript/generated_javascript"),
    ("json", "json/Json.g4", "json/generated"),
    ("json5", "json5/JSON5.g4", "json5/generated"),
    ("kotlin", "kotlin/KotlinParser.g4", "kotlin/generated"),
    ("labels", "labels/Labels.g4", "labels/generated"),
    ("lean", "lean/lean.g4", "lean/generated"),
    ("lua", "lua/LuaParser.g4", "lua/generated"),
    ("mainlang", "modular/MainLang.g4", "modular/generated_mainlang"),
    ("mermaid", "mermaid/mermaidParser.g4", "mermaid/generated"),
    ("nasm", "nasm/nasm_x86_64_Parser.g4", "nasm/generated"),
    ("php", "php/PhpParser.g4", "php/generated"),
    ("preds", "preds/Preds.g4", "preds/generated"),
    ("python", "python/PythonParser.g4", "python/generated"),
    ("rust", "rust/RustParser.g4", "rust/generated"),
    ("splitparser", "modular/SplitParser.g4", "modular/generated_splitparser"),
    ("swift", "swift/Swift5Parser.g4", "swift/generated"),
    ("template", "template/Template.g4", "template/generated"),
    ("typescript", "javascript/TypeScriptParser.g4", "javascript/generated_typescript"),
)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, required=True)
parser.add_argument("--check", action="store_true", help="Verify grammar paths and exit")
args = parser.parse_args()
source = args.source.resolve()
grammars = source / "grammars"
missing = [rel for _, rel, _ in LANGUAGES if not (grammars / rel).is_file()]
if missing:
    sys.exit("Missing grammars: " + ", ".join(missing))
if not (source / "tools" / "lxgen" / "__main__.py").is_file():
    sys.exit(f"lxgen is not in {source / 'tools'}")
if args.check:
    print(f"{len(LANGUAGES)} grammars found in {grammars}")
    raise SystemExit(0)

env = os.environ.copy()
tools = str(source / "tools")
env["PYTHONPATH"] = tools if not env.get("PYTHONPATH") else tools + os.pathsep + env["PYTHONPATH"]
include = str(source / "include")
for lang, grammar, output in LANGUAGES:
    print(f"generate {lang}")
    subprocess.run(
        [sys.executable, "-m", "lxgen", str(grammars / grammar), "-o", str(grammars / output),
         "--id", lang, "--name", lang, "--include-dir", include],
        check=True, env=env)
print(f"Generated {len(LANGUAGES)} languages")
