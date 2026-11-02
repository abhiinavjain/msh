# msh — a POSIX-style Unix shell in C

`msh` began as a fork/exec/wait mini shell built for an OS course. This project grows it into a real shell that follows the POSIX Shell Command Language closely. Its behaviour is checked automatically against **bash**.

## Status

| Feature | State |
|---|---|
| External commands (`fork` / `execvp` / `waitpid`) | ✅ |
| Exit statuses: 127 not found, 126 not executable, 128+N killed by signal | ✅ |
| Quoting: `'…'`, `"…"`, `\` | ✅ |
| Comments (`#`) | ✅ |
| Builtins: `cd [dir\|-]`, `exit [n]`, `export`, `unset` | ✅ |
| Lexer for all operators (`\| \|\| & && ; < > >> <& >& ( )`) | ✅ (not executed yet) |
| Variables: `x=1`, `FOO=1 cmd` (environment for that command only), exported vs shell variables | ✅ |
| Expansion: `$x` `${x}` `$?` `$$` `$#`, `${x:-w}` `${x:=w}` `${x:+w}` `${x:?w}` `${#x}`, `~` `~user` `~+` `~-` | ✅ |
| POSIX field splitting on `IFS`, including empty-field rules | ✅ |
| `$(...)`, `$((...))`, `${x%pat}`-style operators | ⏳ later |
| Parser → AST | ⏳ step 3 |
| Redirections, pipelines, `;` `&&` `\|\|` | ⏳ step 4–5 |
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
| `src/expand.c` | Word expansion in a single pass. Each output byte carries a flag (*literal*, *splittable*, or *quote marker*), then field splitting walks that buffer. Quote removal costs nothing, because quote characters are never copied to the output. |
| `src/vars.c` | Variable table. Shell variables and exported variables live in the same table, told apart by a flag; each child gets a freshly built `envp`. |
| `src/exec.c` | fork/exec/wait, and converting a wait status into an exit status. |
| `src/builtins.c` | Commands that must run inside the shell process (`cd`, `exit`, `export`, `unset`). |
| `src/shell.c/h` | Shell-wide state (`$?`, interactive flag, exit request). |

**Testing strategy:** `tests/compare.sh` feeds each script to both msh and bash, and requires the same stdout, the same exit status, and that both print to stderr or both stay silent. Unit tests cover the lexer and quote removal at the token level.
