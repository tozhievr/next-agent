# Native grammar repairs

These repairs keep the next-langserver lexer/parser ABI. No alternate C scanner is used.

- C: generated predicates all read an unbound environment slot. The override supplies syntax lookahead for declaration/type boundaries. This is syntax analysis, not semantic typedef resolution.
- C#: builtin value-type classification must work without semantic environment bindings. Other type-name alternatives remain syntax-oriented.
- Swift: operator predicates must assemble multiple lexer tokens (for example `-` and `>`), rather than compare only one token to `->`.
- Go: NLSEMI has an epsilon OTHER action. The generated lexer omitted zero-width acceptance and consumed the following character as an error. `tools/build_plugins.py` repairs the generated build copy.
- PHP: quoted parser literals created duplicate implicit token IDs alongside named mode-specific lexer tokens. The parser grammar now references matching named lexer tokens. Regenerated C99 lexer/glue/tables are checked in here. Regenerate using `PYTHONPATH=../next-langserver/tools python -m lxgen vendor/next-langserver/overrides/php/PhpParser.g4 -o vendor/next-langserver/overrides/php/generated --id php --display PhpParser --ext .php --ignore-unknown-preds`.
- Host services: `source/host/services/c_family_outline.hpp` traverses native declaration nodes for names, full ranges, kinds and nested fields. It does not tokenize source text.

Generated frontends still have grammar limits, ignored upstream target-language actions and incomplete semantic binding. The thirty-module matrix is a regression corpus, not proof of complete language support. Mermaid supports ER and class diagrams; its grammar has no flowcharts.
