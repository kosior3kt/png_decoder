#!/bin/bash

sources=($(find src -iname '*.c'))
flags=(-g)
output_name="build/prog"
line_separator="\n============="

# flags handling
[["$1" == "-sanitize" ]] && flags+=(-fsanitize=address)

echo -e "compiling project files:\n${sources[@]} $line_separator"
echo -e "with flags:\n${flags[@]} $line_separator"

gcc  -o "$output_name" "${flags[@]}" "${sources[@]}" libz.a

echo "compilation ended!"
