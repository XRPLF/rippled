#!/usr/bin/env python3

import argparse
import json
import re

from common import eprint


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

    def to_mixed_json(self) -> str:
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

    def to_pure_json(self) -> str:
        '''
        return a pretty printed string as pure json
        '''
        try:
            dict = {}
            dict['t'] = self.timestamp
            dict['m'] = self.module
            dict['l'] = self.level
            dict['msg'] = self.msg
            if self.json_data:
                dict['data'] = self.json_data
            return json.dumps(dict, indent=1)
        except:
            return self.raw_line


def convert_log(in_file_name: str, out_file_name: str, *, pure_json=False):
    try:
        prev_lines = None
        with open(in_file_name) as input:
            with open(out_file_name, "w") as out:
                for l in input:
                    l = l.strip()
                    if not l:
                        continue
                    if LogLine.UNSTRUCTURED_RE.match(l):
                        if prev_lines:
                            log_line = LogLine(prev_lines)
                            if log_line.module == 'SidechainFederator':
                                if pure_json:
                                    print(log_line.to_pure_json(), file=out)
                                else:
                                    print(log_line.to_mixed_json(), file=out)
                        prev_lines = l
                    else:
                        if not prev_lines:
                            eprint(
                                f'Error: Expected prev_lines. Cur line: {l}')
                        assert prev_lines
                        prev_lines += f' {l}'
                if prev_lines:
                    log_line = LogLine(prev_lines)
                    if log_line.module == 'SidechainFederator':
                        if pure_json:
                            print(log_line.to_pure_json(),
                                  file=out,
                                  flush=True)
                        else:
                            print(log_line.to_mixed_json(),
                                  file=out,
                                  flush=True)
    except Exception as e:
        eprint(f'Excption: {e}')
        raise e


def parse_args():
    parser = argparse.ArgumentParser(
        description=('python script to convert log files to json'))

    parser.add_argument(
        '--input',
        '-i',
        help=('input log file'),
    )

    parser.add_argument(
        '--output',
        '-o',
        help=('output log file'),
    )

    return parser.parse_known_args()[0]


if __name__ == '__main__':
    args = parse_args()
    convert_log(args.input, args.output, pure_json=True)
