#!/bin/sh
# Run each test binary and compare its exit status with the expected one.
# Tests named abort_* must die with SIGABRT and segv_* with SIGSEGV;
# everything else must exit 0. If test/<name>.expected exists, the test's
# standard output must match it exactly.
dir=$(dirname "$0")
out=$(mktemp)
trap 'rm -f "$out"' EXIT
pass=0 fail=0
for t in "$@"; do
	name=${t##*/}
	case $name in
	abort_*) want=134 ;;
	segv_*) want=139 ;;
	*) want=0 ;;
	esac
	# Run in a subshell so a fatal signal is reported as 128+signo, and
	# silence the expected crash diagnostics.
	if [ $want -eq 0 ]; then
		(CITADEL_TEST=1 "$t" alpha beta) >"$out" ; got=$?
	else
		(CITADEL_TEST=1 "$t" alpha beta) >"$out" 2>/dev/null ; got=$?
	fi
	exp="$dir/$name.expected"
	if [ -f "$exp" ] && ! cmp -s "$exp" "$out"; then
		fail=$((fail + 1))
		echo "FAIL $t (output differs from $exp)"
		diff "$exp" "$out" | head -5
		continue
	fi
	if [ $got -eq $want ]; then
		pass=$((pass + 1))
		echo "PASS $t"
	else
		fail=$((fail + 1))
		echo "FAIL $t (exit $got, want $want)"
	fi
done
echo "$pass passed, $fail failed"
[ $fail -eq 0 ]
