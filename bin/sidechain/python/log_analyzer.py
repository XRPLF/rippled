#!/usr/bin/env python3

import argparse
import json
import os
import re
import sys

from common import eprint
from typing import IO, Optional


class LogLine:
    UNSTRUCTURED_RE = re.compile(r'''(?x)
                # The x flag enables insignificant whitespace mode (allowing comments)
                ^(?P<timestamp>.*UTC)
                [\ ]
                (?P<module>[^:]*):(?P<level>[^\ ]*)
                [\ ]
                (?P<msg>.*$)
                ''')

    STRUCTURED_RE = re.compile(r'''(?x)
                # The x flag enables insignificant whitespace mode (allowing comments)
                ^(?P<timestamp>.*UTC)
                [\ ]
                (?P<module>[^:]*):(?P<level>[^\ ]*)
                [\ ]
                (?P<msg>[^{]*)
                [\ ]
                (?P<json_data>.*$)
                ''')

    def __init__(self, line: str):
        self.raw_line = line
        self.json_data = None

        try:
            if line.endswith('}'):
                m = self.STRUCTURED_RE.match(line)
                try:
                    self.json_data = json.loads(m.group('json_data'))
                except:
                    m = self.UNSTRUCTURED_RE.match(line)
            else:
                m = self.UNSTRUCTURED_RE.match(line)

            self.timestamp = m.group('timestamp')
            self.level = m.group('level')
            self.module = m.group('module')
            self.msg = m.group('msg')
        except Exception as e:
            eprint(f'init exception: {e} line: {line}')

    def to_mixed_json_str(self) -> str:
        '''
        return a pretty printed string as mixed json
        '''
        try:
            r = f'{self.timestamp} {self.module}:{self.level} {self.msg}'
            if self.json_data:
                r += '\n' + json.dumps(self.json_data, indent=1)
            return r
        except:
            eprint(f'Using raw line: {self.raw_line}')
            return self.raw_line

    def to_pure_json(self) -> dict:
        '''
        return a json dict
        '''
        dict = {}
        dict['t'] = self.timestamp
        dict['m'] = self.module
        dict['l'] = self.level
        dict['msg'] = self.msg
        if self.json_data:
            dict['data'] = self.json_data
        return dict

    def to_pure_json_str(self, f_id: Optional[str] = None) -> str:
        '''
        return a pretty printed string as pure json
        '''
        try:
            dict = self.to_pure_json(f_id)
            return json.dumps(dict, indent=1)
        except:
            return self.raw_line


def convert_log(in_file_name: str,
                out: str,
                *,
                as_list=False,
                pure_json=False,
                module: Optional[str] = 'SidechainFederator') -> list:
    result = []
    try:
        prev_lines = None
        with open(in_file_name) as input:
            for l in input:
                l = l.strip()
                if not l:
                    continue
                if LogLine.UNSTRUCTURED_RE.match(l):
                    if prev_lines:
                        log_line = LogLine(prev_lines)
                        if not module or log_line.module == module:
                            if as_list:
                                result.append(log_line.to_pure_json())
                            else:
                                if pure_json:
                                    print(log_line.to_pure_json_str(),
                                          file=out)
                                else:
                                    print(log_line.to_mixed_json_str(),
                                          file=out)
                    prev_lines = l
                else:
                    if not prev_lines:
                        eprint(f'Error: Expected prev_lines. Cur line: {l}')
                    assert prev_lines
                    prev_lines += f' {l}'
            if prev_lines:
                log_line = LogLine(prev_lines)
                if not module or log_line.module == module:
                    if as_list:
                        result.append(log_line.to_pure_json())
                    else:
                        if pure_json:
                            print(log_line.to_pure_json_str(f_id),
                                  file=out,
                                  flush=True)
                        else:
                            print(log_line.to_mixed_json_str(),
                                  file=out,
                                  flush=True)
    except Exception as e:
        eprint(f'Excption: {e}')
        raise e
    return result


def convert_all(in_dir_name: str, out: IO, *, pure_json=False):
    '''
    Convert all the "debug.log" log files in one directory level below the in_dir_name into a single json file.
    There will be a field called 'f' for the director name that the origional log file came from.
    This is useful when analyzing networks that run on the local machine.
    '''
    if not os.path.isdir(in_dir_name):
        print(f'Error: {in_dir_name} is not a directory')
    files = []
    f_ids = []
    for subdir in os.listdir(in_dir_name):
        file = f'{in_dir_name}/{subdir}/debug.log'
        if not os.path.isfile(file):
            continue
        files.append(file)
        f_ids.append(subdir)

    result = {}
    for f, f_id in zip(files, f_ids):
        l = convert_log(f, out, as_list=True, pure_json=pure_json, module=None)
        result[f_id] = l
    print(json.dumps(result, indent=1), file=out, flush=True)


def parse_args():
    parser = argparse.ArgumentParser(
        description=('python script to convert log files to json'))

    parser.add_argument(
        '--input',
        '-i',
        help=('input log file or sidechain config directory structure'),
    )

    parser.add_argument(
        '--output',
        '-o',
        help=('output log file'),
    )

    return parser.parse_known_args()[0]


if __name__ == '__main__':
    try:
        args = parse_args()
        with open(args.output, "w") as out:
            if os.path.isdir(args.input):
                convert_all(args.input, out, pure_json=True)
            else:
                convert_log(args.input, out, pure_json=True)
    except Exception as e:
        eprint(f'Excption: {e}')
        raise e
