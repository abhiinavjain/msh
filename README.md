# msh — a POSIX-style Unix shell in C

`msh` began as a fork/exec/wait mini shell built for an OS course. This project grows it into a real shell that follows the POSIX Shell Command Language closely. Its behaviour is checked automatically against **bash**.

## Status

| Feature | State |
|---|---|
| External commands (`fork` / `execvp` / `waitpid`) | ✅ |
| Exit statuses: 127 not found, 126 not executable, 128+N killed by signal | ✅ |
| Quoting: `'…'`, `"…"`, `\` | ✅ |
| Comments (`#`) | ✅ |
| Builtins: `cd [dir|-]`, `exit [n]` | ✅ |
| Lexer for all operators (`| || & && ; < > >> <& >& ( )`) | ✅ (not executed yet) |
| `$VAR`, `$?`, `~` expansion | ⏳ step 2 |
| Parser → AST | ⏳ step 3 |
| Redirections, pipelines, `;` `&&` `||` | ⏳ |
| Job control (`&`, `jobs`, `fg`, `bg`, Ctrl-C/Ctrl-Z) | ⏳ |
| Hand-written line editor (termios), history, tab completion | ⏳ |

## Build and test

```sh
make          # ./msh
make test     # unit tests + bash comparison, built with ASan/UBSan
```

## Design

```
line ──lex──> tokens ──parse──> AST ──expand──> argv ──exec──> status
```

| File | Role |
|---|---|
| `src/lexer.c` | Splits a line into tokens using the POSIX §2.3 rules. Quotes are *recognised* here but kept in the text. |
| `src/expand.c` | Word expansion. Quote removal runs last, as POSIX §2.6 requires. |
| `src/exec.c` | fork/exec/wait, and converting a wait status into an exit status. |
| `src/builtins.c` | Commands that must run inside the shell process (`cd`, `exit`). |
| `src/shell.c/h` | Shell-wide state (`$?`, interactive flag, exit request). |

**Testing strategy:** `tests/compare.sh` feeds each script to both msh and bash, and requires the same stdout, the same exit status, and that both print to stderr or both stay silent. Unit tests cover the lexer and quote removal at the token level.
