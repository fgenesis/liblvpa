#!/bin/sh

# run this from parent directory
# quick compression test on the zlib source files

dozip() {
    exe="$1"
    param="$2"
	shift 2
	"./$exe" c "benchmark_out/$param.lvpa" -H$param -s "$@" 
}

benchrun() {
    exe="$1"
    shift
    for x in 1 2 3 4 5 9; do
        for y in lzma lzo zip; do
            dozip "$exe" "$y$x" "$@"
        done
    done
    # lzf does not have compression levels
    dozip "$exe" lzf "$@"
}

if [ "x$1" == "x" ]; then
    echo "Usage: ./paktest.sh \"/path/to/lvpak\" [additional args for each lvpak call]"
    echo "You can supply additional files for testing, for example."
    exit
fi

lvpak="$1"
shift

mkdir -p benchmark_out

benchrun "$lvpak" zlib lvpa lzo lzma "$@"

