from typing import List, Optional, Tuple


class Section:
    def section_header(l: str) -> Optional[str]:
        '''
        If the line is a section header, return the section name
        otherwise return None
        '''
        if l.startswith('[') and l.endswith(']'):
            return l[1:-1]
        return None

    def __init__(self, name: str):
        super().__setattr__('_name', name)
        # lines contains all non key-value pairs
        super().__setattr__('_lines', [])
        super().__setattr__('_kv_pairs', {})

    def get_name(self):
        return self._name

    def add_line(self, l):
        s = l.split('=')
        if len(s) == 2:
            self._kv_pairs[s[0].strip()] = s[1].strip()
        else:
            self._lines.append(l)

    def get_lines(self):
        return self._lines

    def get_line(self) -> Optional[str]:
        if len(self._lines) > 0:
            return self._lines[0]
        return None

    def __getattr__(self, name):
        try:
            return self._kv_pairs[name]
        except KeyError:
            raise AttributeError(name)

    def __setattr__(self, name, value):
        if name in self.__dict__:
            super().__setattr__(name, value)
        else:
            self._kv_pairs[name] = value

    def __getstate__(self):
        return vars(self)

    def __setstate__(self, state):
        vars(self).update(state)


class ConfigFile:
    def __init__(self, *, file_name: Optional[str] = None):
        # parse the file
        self._file_name = file_name
        self._sections = {}
        if not file_name:
            return

        cur_section = None
        with open(file_name) as f:
            for n, l in enumerate(f):
                l = l.strip()
                if l.startswith('#') or not l:
                    continue
                if section_name := Section.section_header(l):
                    if cur_section:
                        self.add_section(cur_section)
                    cur_section = Section(section_name)
                    continue
                if not cur_section:
                    raise ValueError(
                        f'Error parsing config file: {file_name} line_num: {n} line: {l}'
                    )
                cur_section.add_line(l)

        if cur_section:
            self.add_section(cur_section)

    def add_section(self, s: Section):
        self._sections[s.get_name()] = s

    def get_file_name(self):
        return self._file_name

    def __getstate__(self):
        return vars(self)

    def __setstate__(self, state):
        vars(self).update(state)

    def __getattr__(self, name):
        try:
            return self._sections[name]
        except KeyError:
            raise AttributeError(name)
