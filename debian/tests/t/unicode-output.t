#!/bin/sh

echo 1..$(ls -1 $(dirname $0)/*.txt | wc -l)

. `dirname $0`/boilerplate.sh

LC_ALL=C.UTF-8
export LC_ALL
TERM=xterm
export TERM

for file in $(ls -1 $(dirname $0)/*.txt) ; do
    echo === BEGIN $file ===
    cat $file
    echo === END $file ===
    echo "=== BEGIN $file (escaped)  ==="
    cat -v $file
    echo "=== END $file (escaped) ==="
    script -c "$SCREEN cat $file" ${file}.output | cat -v
    fgrep -F -f $file -q ${file}.output
    check_exit_code_true Found contents of $file in ${file}.output
    rm -f ${file}.output
done
