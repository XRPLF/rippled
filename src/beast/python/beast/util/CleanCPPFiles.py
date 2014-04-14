from __future__ import absolute_import, division, print_function, unicode_literals

#------------------------------------------------------------------------------
#
#    This file is part of Beast: https://github.com/vinniefalco/Beast
#    Copyright 2014, Tom Ritchford <tom@swirly.com>
#
#    Permission to use, copy, modify, and/or distribute this software for any
#    purpose  with  or without fee is hereby granted, provided that the above
#    copyright notice and this permission notice appear in all copies.
#
#    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
#    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
#    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
#    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
#    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
#    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
#    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#------------------------------------------------------------------------------

"""
Cleans C++ files according to a consistent standard.

Trims trailing whitespace; converts files encoded in Latin-1 to UTF-8.

"""

import os
import sys
import tempfile

def process_file(filename, process):
    with open(filename) as infile:
        lines = infile.read().strip().splitlines()
        outfd, outname = tempfile.mkstemp(dir=os.path.dirname(filename))
        with os.fdopen(outfd, 'w') as outfile:
            for number, line in enumerate(lines):
                try:
                    result = process(line, filename, number).encode('utf-8')
                    outfile.write(result)
                except Exception as e:
                    if True: raise
                    raise Exception('%s on line %d in file %s' %
                                    (e.message, number + 1, filename))
    os.rename(outname, filename)

def walk_and_process(root, process, condition, message=None):
    for root, dirs, files in os.walk(root):
        for f in files:
            filename = os.path.join(root, f)
            if condition(filename):
                if message:
                    print(message % filename)
                process_file(filename, process)

def clean_line_endings(root, endings=None, message=None):
    if endings == None:
        endings = '.h', '.cpp', '.sh'
    def condition(filename):
        return os.path.splitext(filename)[1] in endings

    def process(line, filename, number):
        try:
            return line.decode('utf-8').rstrip() + '\n'
        except:
            result = line.decode('latin-1').rstrip() + '\n'
            print('Found a non UTF-8 line at %s:%d' % (filename, number + 1))
            return result

    walk_and_process(root, process, condition, message)

if __name__ == "__main__":
    path = '.'
    if len(sys.argv) > 1:
        path = sys.argv[1]
    clean_line_endings(os.path.abspath(path)) #, message='processing %s')
