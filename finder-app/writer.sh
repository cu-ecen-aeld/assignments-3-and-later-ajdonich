#!/bin/bash

# Validate command line
if [ $# -ne 2 ]; then
    echo "usage: writer.sh <path/to/file> <expression>"
    exit 1
fi

FILE=${1}
EXPRESSION=${2}

# Create dir part of path/to/file
mkdir -p $(dirname "${FILE}")
if [ $?  -ne 0 ]; then
    echo "ERROR: unable to create directory ${FILE}"
    exit 1
fi

# Create/write/overwrite file
echo "${EXPRESSION}" > ${FILE}
if [ $?  -ne 0 ]; then
    echo "ERROR: unable to create or write to ${FILE}"
    exit 1
fi

exit 0
