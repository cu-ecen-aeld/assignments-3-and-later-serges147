#!/bin/sh
set -e

if [ $# -ne 2 ]; then
    echo "2 arguments expected"
    exit 1
fi

filesdir=$1
searchstr=$2

if [ -d ${filesdir} ]
then
    x=$( find ${filesdir} -type f | wc -l )
    y=$( grep -r "${searchstr}" "${filesdir}" | wc -l )
    echo "The number of files are ${x} and the number of matching lines are ${y}"
else
    echo "not a directory"
    exit 1
fi
