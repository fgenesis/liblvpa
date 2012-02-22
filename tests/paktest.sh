#!/bin/sh

# run this from parent directory
# quick compression test for user-specified files
# to get started, copy an lvpak binary into the liblvpa main directory and from there, use:
#   $ tests/paktest.sh lvpak.exe lvpa lzma lzham lzo
# to compress about 1.5 MB source code.

dozip() {
    exe="$1"
    param="$2"
	shift 2
	"./$exe" c "benchmark_out/$param.lvpa" -H$param -s "$@" 
}

benchrun() {
    exe="$1"
    shift
    dozip "$exe" 0 "$@"    # uncompressed
    dozip "$exe" lzf "$@"  # lzf does not have compression levels
    for x in 1 2 3 4 5 9; do            # levels
        for y in lzma lzo zip lzham; do # algorithms
            dozip "$exe" "$y$x" "$@"
        done
    done

}

if [ "x$1" == "x" ]; then
    echo "Usage: tests/paktest.sh \"/path/to/lvpak\" [additional args for each lvpak call]"
    echo "You can supply additional files for testing, for example."
    exit
fi

lvpak="$1"
shift

mkdir -p benchmark_out

benchrun "$lvpak" "$@"

