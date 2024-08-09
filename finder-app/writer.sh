#!/bin/bash

if [ "$1" = "" ] || [ "$2" = "" ]; then
    echo Plese specify at least two arguments
    echo "USAGE:finder.sh <file full path> <text to find>"
    exit 1
fi

dir_path=`echo "${1%/*}"`
mkdir -p "$dir_path"
echo "$2" > "$1"

if [ "$?" -ne 0 ]; then
    echo "ERROR! Could not write \""$2"\" to \""$1"\""
    exit 1
fi

exit 0
