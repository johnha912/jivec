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

echo === TEST ON VARS.JIVE ===

./jive ../jive_programs/vars.jive -o vars.asm
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Compiler returned an error compiling vars.jive
	exit $ret_val
fi

# Stage 4 sample: assemble + link + run vars.jive and check it exits 42.
nasm -felf64 vars.asm -o vars.o
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Assembler failed to assemble vars.asm
	exit $ret_val
fi

ld vars.o -o vars
ret_val=$?
if [ $ret_val -ne 0 ]; then
	echo ERROR: Linker failed to link vars.o
	exit $ret_val
fi

./vars
ret_val=$?
if [ $ret_val -ne 42 ]; then
	echo "ERROR: vars exited with $ret_val (expected 42)"
	exit 1
fi
echo "vars exited with 42 as expected"

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
