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

    # --- step 2: variables and assignments ---
    $'x=hello\necho $x'
    $'x=1\nx=2 y=3\necho $x $y'
    $'x=5\necho ${x}yz $xyz end'
    $'echo [$unset_var_xyz]'
    $'x=a\necho $x'
    $'"x"=1'
    $'x=1 y=$x\necho [$y]'
    $'A=1 B=$A sh -c \'echo [$A][$B]\''
    $'export A=1\nsh -c \'echo $A\''
    $'B=2 sh -c \'echo $B\'\necho [$B]'
    $'C=3\nsh -c \'echo [$C]\'\nexport C\nsh -c \'echo [$C]\''
    $'export D\nD=4\nsh -c \'echo [$D]\''
    $'export 1x=2'
    $'export 1x=2 OK=1\nsh -c \'echo [$OK]\''
    $'x=1\nunset x\necho [$x]'
    $'export E=1\nunset E\nsh -c \'echo [$E]\''
    $'unset 1x'
    $'HOME=/usr cd\npwd\necho $HOME'
    $'x=1\nx=2 export y\necho $x'
    $'A=1 A=2 sh -c \'echo $A\'\necho [$A]'
    $'x=old\nx=new echo $x'
    $'x=old\nx=new y=$x sh -c \'echo $x $y\'\necho $x [$y]'
    $'unset -v 1x'
    $'unset "a b" 1x\necho $?'
    $'PATH=/nonexistent ls'
    $'cd /tmp\nsh -c \'echo [$OLDPWD]\''
    $'unset HOME\ncd'

    # --- special parameters ---
    $'false\necho $?\necho $?'
    $'sh -c "exit 7"\necho $?'
    $'nonexistent_cmd_xyz\necho $?'
    $'echo $# ${#}'
    $'test $$ -gt 1'
    $'x=$$\nsh -c "test \\$PPID = $x"'
    $'echo $ "$" a$ $% $/ "a$"'
    $'echo $1 $9 [$10]'

    # --- quoting meets expansion ---
    $'x=world\necho "hello $x" \'hello $x\' hello\\ \\$x'
    $'x=world\necho "${x}s" "$xs" "\\$x"'
    $'x="a   b"\nprintf "[%s]" $x "$x"\necho'
    $'x=" lead trail "\nprintf "[%s]" $x\necho'
    $'x=\nprintf "[%s]" $x "$x" \'\' $x""\necho'
    $'x="a "\nprintf "[%s]" $x""\necho'
    $'x="a b"\nprintf "[%s]" pre$x"post"\necho'

    # --- field splitting and IFS ---
    $'IFS=:\nx=":a::b:"\nprintf "[%s]" $x\necho'
    $'IFS=" :"\nx=" a : b  c:"\nprintf "[%s]" $x\necho'
    $'IFS=\nx="a b"\nprintf "[%s]" $x\necho'
    $'unset IFS\nx="a  b"\nprintf "[%s]" $x\necho'
    $'x="a:b"\nIFS=: printf "[%s]" $x\necho'
    $'IFS=x\ny=axbxc\nprintf "[%s]" $y a"x"b\necho'

    # --- ${...} forms ---
    $'x=val\necho ${x:-d} ${u:-d} ${x-d} ${u-d}'
    $'x=\necho [${x:-d}] [${x-d}]'
    $'x=val\necho [${x:+alt}] [${u:+alt}] [${u+alt}]'
    $'x=\necho [${x:+alt}] [${x+alt}]'
    $'echo ${u:=7} $u'
    $'x=\necho [${x=7}] [${x:=8}] $x'
    $'printf "[%s]" "${u:-a b}" ${u:-a b} ${u:-"a b"}\necho'
    $'x=hello\necho ${#x} ${#u}'
    $'d=dflt\necho ${u:-$d} "${u:-$d}" ${u:-${d}x}'
    $'echo ${u:-\'$lit\'}'
    $'echo ${u:-}x'
    $'echo ${x:?custom msg}\necho after'
    $'x=set\necho ${x:?msg}\necho after'
    $'echo ${1x}\necho after'
    $'echo ${x'

    # --- tilde ---
    $'echo ~ ~/a "~" \'~\' \\~ a~ ~nouser_xyz_ab'
    $'echo ~root'
    $'cd /tmp\necho ~+ ~-'
    $'cd /tmp\ncd /\necho ~-'
    $'x=~/a:~/b\necho $x'
    $'x=~\necho "$x"'
    $'HOME="/a b"\nprintf "[%s]" ~ ~/c\necho'
    $'x=\\~\necho $x'
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
