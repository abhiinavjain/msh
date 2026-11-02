#!/usr/bin/env bash
#
# compare.sh — run each test script through msh AND bash, and require
# identical stdout and identical exit status.
#
# Using bash as the reference ("oracle") means we never hand-write
# expected output: if msh disagrees with bash, msh is wrong (or the
# case documents a deliberate difference — none so far).
#
# stderr is NOT compared, because error messages start with "msh:" vs
# "bash: line 1:". We only require that both did or did not print errors.
#
# Version-dependent cases: if a script starts with the line "@status=N",
# stdout and stderr are still compared with bash, but msh's exit status
# must be exactly N, whatever this machine's bash returns. Use this only
# when bash itself changed between releases.
#
# usage: tests/compare.sh ./msh

set -u
MSH=$(realpath "${1:-./msh}")

pass=0
fail=0

# Each case is one shell script (may contain newlines).
CASES=(
    # --- plain commands (PA3 behaviour) ---
    $'/bin/echo hello world'
    $'echo   spaced     out'
    $'true'
    $'false'
    $''
    $'\n\n   \n'

    # --- quoting ---
    $'echo "a   b"'
    $'echo \'a   b\''
    $'echo a\\ \\ b'
    $'echo "x\\"y"'
    $'echo \'no $escapes \\n here\''
    $'echo pre\'mid\'"post"'
    $'echo "" x ""'
    $'echo "\'" \'"\''
    $'echo a\\|b a\\;b a\\>b'
    $'echo "a|b" \'c;d\' "e>f"'
    $'printf "%s\\n" "one arg" two'

    # --- comments ---
    $'echo visible # hidden'
    $'# whole line comment\necho after'
    $'echo a#b'

    # --- exit statuses ---
    $'definitely_not_a_command_xyz'
    $'/no/such/path/prog'
    $'sh -c "exit 42"'
    $'sh -c \'kill -9 $$\''
    $'sh -c \'kill -TERM $$\''
    $'false\ntrue'
    $'true\nfalse'

    # --- exit builtin ---
    $'exit'
    $'false\nexit'
    $'exit 3\necho unreachable'
    $'exit 300'
    $'exit -1'
    $'exit abc'
    $'exit 1 2\necho still here'

    # --- cd builtin ---
    $'cd /tmp\npwd'
    $'cd /\ncd /tmp\ncd -\npwd'
    $'cd\npwd'
    $'cd /definitely/not/here'
    $'cd /definitely/not/here\npwd'
    $'@status=2\ncd a b'          # bash 5.2 says 1, bash 5.3 says 2
    $'cd /tmp\nsh -c \'echo $PWD\''
)

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT

run() {  # run <shell...> ; script on stdin; captures into globals
    local out err
    out=$(cd "$workdir" && HOME="$workdir" "$@" 2>"$workdir/.err"; echo "[status=$?]")
    err=$(cat "$workdir/.err")
    R_OUT=$out
    R_ERR=$err
}

for script in "${CASES[@]}"; do
    want_status=
    if [[ $script =~ ^@status=([0-9]+)$'\n'(.*)$ ]]; then
        want_status=${BASH_REMATCH[1]}
        script=${BASH_REMATCH[2]}
    fi

    run "$MSH" <<<"$script"
    m_out=$R_OUT m_err=$R_ERR
    run bash --norc --noprofile <<<"$script"
    b_out=$R_OUT b_err=$R_ERR

    if [[ -n $want_status ]]; then
        # Swap bash's status for the pinned one before comparing.
        b_out="${b_out%\[status=*\]}[status=$want_status]"
    fi

    # Both quiet on stderr, or both noisy.
    m_noisy=$([[ -n $m_err ]] && echo y || echo n)
    b_noisy=$([[ -n $b_err ]] && echo y || echo n)

    if [[ $m_out == "$b_out" && $m_noisy == "$b_noisy" ]]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL: %q\n' "$script"
        printf '  bash: %q  (stderr: %s)\n' "$b_out" "$b_noisy"
        printf '  msh:  %q  (stderr: %s)\n' "$m_out" "$m_noisy"
        [[ -n $m_err ]] && printf '  msh stderr: %s\n' "$m_err"
    fi
done

echo "compare: $pass passed, $fail failed"
[[ $fail -eq 0 ]]
