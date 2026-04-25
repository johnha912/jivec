#!/bin/bash
# Build the jivec compiler and smoke-test it on the sample programs.
# Run from inside the code/ directory:
#     cd code && ./build.sh

# Make sure ../build exists before pushd'ing into it.
mkdir -p ../build

pushd ../build > /dev/null

gcc -Wall -ggdb ../code/main.c -o jive
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Failed to build
	exit $ret_val
fi

echo "Build success!"

echo === TEST ON SIMPLE.JIVE ===

./jive ../jive_programs/simple.jive -o simple.asm
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Compiler returned an error compiling simple.jive
	exit $ret_val
fi

echo === TEST ON SIMPLE2.JIVE ===

./jive ../jive_programs/simple2.jive -o simple2.asm
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Compiler returned an error compiling simple2.jive
	exit $ret_val
fi

echo === TEST ON EXPR.JIVE ===

./jive ../jive_programs/expr.jive -o expr.asm
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Compiler returned an error compiling expr.jive
	exit $ret_val
fi

# Helper: compile a .jive program, assemble + link, run it, and assert the
# exit code matches the expected value.
run_and_check_exit() {
	local program=$1
	local expected=$2
	local stem=${program%.jive}

	echo === TEST ON ${program^^} ===

	./jive ../jive_programs/$program -o $stem.asm
	if [ $? -ne 0 ]; then
		echo "ERROR: compiler failed on $program"
		exit 1
	fi
	nasm -felf64 $stem.asm -o $stem.o
	if [ $? -ne 0 ]; then
		echo "ERROR: nasm failed on $stem.asm"
		exit 1
	fi
	ld $stem.o -o $stem
	if [ $? -ne 0 ]; then
		echo "ERROR: ld failed on $stem.o"
		exit 1
	fi
	./$stem
	local actual=$?
	if [ $actual -ne $expected ]; then
		echo "ERROR: $stem exited with $actual (expected $expected)"
		exit 1
	fi
	echo "$stem exited with $expected as expected"
}

run_and_check_exit vars.jive 42
run_and_check_exit fn_calls.jive 25
run_and_check_exit fib.jive 88

echo === ERROR-CASE TESTS ===

# These programs are expected to fail compilation. We invert the check.
for err_program in err_set_undeclared.jive err_use_undeclared.jive err_redeclared.jive; do
	./jive ../jive_programs/$err_program -o /tmp/jive_err.asm 2>/dev/null
	if [ $? -eq 0 ]; then
		echo "ERROR: $err_program was expected to fail but compiled successfully"
		exit 1
	fi
	echo "$err_program failed to compile as expected"
done

popd > /dev/null
