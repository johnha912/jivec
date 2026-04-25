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

# Stage 4 will enable assembling + linking the .asm output.
# nasm -felf64 simple.asm
# ret_val=$?
# if [ $ret_val -ne 0 ]; then
# 	echo ERROR: Assembler failed to assemble simple.asm
# 	exit $ret_val
# fi

# ld simple.o -o simple
# ret_val=$?
# if [ $ret_val -ne 0 ]; then
# 	echo ERROR: Linker failed to link simple.o
# 	exit $ret_val
# fi

popd > /dev/null
