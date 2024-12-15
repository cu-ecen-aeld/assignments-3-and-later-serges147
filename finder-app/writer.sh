#!/bin/bash
set -e

if [ $# -ne 2 ]; then
    echo "2 arguments expected"
    exit 1
fi

writefile=$1
writestr=$2

echo ${writestr} > ${writefile}
