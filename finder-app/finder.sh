#!/bin/sh

if [ "$1" = "" ] || [ "$2" = "" ]; then
    echo Plese specify at least two arguments
    echo "USAGE:finder.sh <directory path> <text to find>"
    exit 1
fi

if [ ! -d "$1" ]; then
    echo \""$1"\" is not a directory!
    exit 1
fi

num_files=`find -L $1/* ! -type d | wc -l`
lines_match=`grep -r "$2" $1/ | wc -l`
echo The number of files are "$num_files" and the number of matching lines are "$lines_match"
exit 0
