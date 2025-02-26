#!/bin/sh
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
#
# install.sh -- install a program, script or datafile
#
# Based on `install-sh' from the X Consortium's X11R5 distribution
# as of 89/12/18 which is freely available.
# Cleaned up for Apache's Autoconf-style Interface (APACI)
# by Ralf S. Engelschall <rse apache.org>

#
#   put in absolute paths if you don't have them in your path; 
#   or use env. vars.
#
mvprog="${MVPROG-mv}"
cpprog="${CPPROG-cp}"
chmodprog="${CHMODPROG-chmod}"
chownprog="${CHOWNPROG-chown}"
chgrpprog="${CHGRPPROG-chgrp}"
stripprog="${STRIPPROG-strip}"
rmprog="${RMPROG-rm}"

#
#   parse argument line
#
instcmd="$mvprog"
chmodcmd=""
chowncmd=""
chgrpcmd=""
stripcmd=""
rmcmd="$rmprog -f"
mvcmd="$mvprog"
ext=""
src=""
dst=""
while [ "x$1" != "x" ]; do
    case $1 in
        -c) instcmd="$cpprog"
            shift; continue
            ;;
        -m) chmodcmd="$chmodprog $2"
            shift; shift; continue
            ;;
        -o) chowncmd="$chownprog $2"
            shift; shift; continue
            ;;
        -g) chgrpcmd="$chgrpprog $2"
            shift; shift; continue
            ;;
        -s) stripcmd="$stripprog"
            shift; continue
            ;;
        -S) stripcmd="$stripprog $2"
            shift; shift; continue
            ;;
        -e) ext="$2"
            shift; shift; continue
            ;;
        *)  break
            ;;
    esac
done
if test $# -eq 0 ; then
    echo "install.sh: no input file(s) specified"
    exit 1
fi
if test $# -eq 1 ; then
    echo "install.sh: no destination specified"
    exit 1
fi
for arg in "$@" ; do
    dstarg="$arg"
done

while test $# -gt 1 ; do
    dst="$dstarg"
    src="$1"
    shift
    #
    #  If destination is a directory, append the input filename; if
    #  your system does not like double slashes in filenames, you may
    #  need to add some logic
    #
    if [ -d $dst ]; then
        dst="$dst/`basename $src`"
    fi

    #  Add a possible extension (such as ".exe") to src and dst
    src="$src$ext"
    dst="$dst$ext"

    #  Make a temp file name in the proper directory.
    dstdir=`dirname $dst`
    dsttmp=$dstdir/#inst.$$#

    #  Move or copy the file name to the temp name
    $instcmd $src $dsttmp

    #  And set any options; do chmod last to preserve setuid bits
    if [ "x$chowncmd" != "x" ]; then $chowncmd $dsttmp; fi
    if [ "x$chgrpcmd" != "x" ]; then $chgrpcmd $dsttmp; fi
    if [ "x$stripcmd" != "x" ]; then $stripcmd $dsttmp; fi
    if [ "x$chmodcmd" != "x" ]; then $chmodcmd $dsttmp; fi

    #  Now rename the file to the real destination.
    $rmcmd $dst
    $mvcmd $dsttmp $dst

done

exit 0

