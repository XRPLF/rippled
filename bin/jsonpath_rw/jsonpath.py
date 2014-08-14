from __future__ import unicode_literals, print_function, absolute_import, division, generators, nested_scopes
import logging
import six
from six.moves import xrange
from itertools import *

logger = logging.getLogger(__name__)

# Turn on/off the automatic creation of id attributes
# ... could be a kwarg pervasively but uses are rare and simple today
auto_id_field = None

class JSONPath(object):
    """
    The base class for JSONPath abstract syntax; those
    methods stubbed here are the interface to supported 
    JSONPath semantics.
    """

    def find(self, data):
        """
        All `JSONPath` types support `find()`, which returns an iterable of `DatumInContext`s.
        They keep track of the path followed to the current location, so if the calling code
        has some opinion about that, it can be passed in here as a starting point.
        """
        raise NotImplementedError()

    def update(self, data, val):
        "Returns `data` with the specified path replaced by `val`"
        raise NotImplementedError()

    def child(self, child):
        """
        Equivalent to Child(self, next) but with some canonicalization
        """
        if isinstance(self, This) or isinstance(self, Root):
            return child
        elif isinstance(child, This):
            return self
        elif isinstance(child, Root):
            return child
        else:
            return Child(self, child)

    def make_datum(self, value):
        if isinstance(value, DatumInContext):
            return value
        else:
            return DatumInContext(value, path=Root(), context=None)

class DatumInContext(object):
    """
    Represents a datum along a path from a context.

    Essentially a zipper but with a structure represented by JsonPath, 
    and where the context is more of a parent pointer than a proper 
    representation of the context.

    For quick-and-dirty work, this proxies any non-special attributes
    to the underlying datum, but the actual datum can (and usually should)
    be retrieved via the `value` attribute.

    To place `datum` within another, use `datum.in_context(context=..., path=...)`
    which extends the path. If the datum already has a context, it places the entire
    context within that passed in, so an object can be built from the inside
    out.
    """
    @classmethod
    def wrap(cls, data):
        if isinstance(data, cls):
            return data
        else:
            return cls(data)

    def __init__(self, value, path=None, context=None):
        self.value = value
        self.path = path or This()
        self.context = None if context is None else DatumInContext.wrap(context)

    def in_context(self, context, path):
        context = DatumInContext.wrap(context)

        if self.context:
            return DatumInContext(value=self.value, path=self.path, context=context.in_context(path=path, context=context))
        else:
            return DatumInContext(value=self.value, path=path, context=context)

    @property
    def full_path(self):
        return self.path if self.context is None else self.context.full_path.child(self.path)

    @property
    def id_pseudopath(self):
        """
        Looks like a path, but with ids stuck in when available
        """
        try:
            pseudopath = Fields(str(self.value[auto_id_field]))
        except (TypeError, AttributeError, KeyError): # This may not be all the interesting exceptions
            pseudopath = self.path

        if self.context:
            return self.context.id_pseudopath.child(pseudopath)
        else:
            return pseudopath

    def __repr__(self):
        return '%s(value=%r, path=%r, context=%r)' % (self.__class__.__name__, self.value, self.path, self.context)

    def __eq__(self, other):
        return isinstance(other, DatumInContext) and other.value == self.value and other.path == self.path and self.context == other.context

class AutoIdForDatum(DatumInContext):
    """
    This behaves like a DatumInContext, but the value is
    always the path leading up to it, not including the "id",
    and with any "id" fields along the way replacing the prior 
    segment of the path

    For example, it will make "foo.bar.id" return a datum
    that behaves like DatumInContext(value="foo.bar", path="foo.bar.id").

    This is disabled by default; it can be turned on by
    settings the `auto_id_field` global to a value other
    than `None`. 
    """
    
    def __init__(self, datum, id_field=None):
        """
        Invariant is that datum.path is the path from context to datum. The auto id
        will either be the id in the datum (if present) or the id of the context
        followed by the path to the datum.

        The path to this datum is always the path to the context, the path to the
        datum, and then the auto id field.
        """
        self.datum = datum
        self.id_field = id_field or auto_id_field

    @property
    def value(self):
        return str(self.datum.id_pseudopath)

    @property
    def path(self):
        return self.id_field

    @property
    def context(self):
        return self.datum

    def __repr__(self):
        return '%s(%r)' % (self.__class__.__name__, self.datum)

    def in_context(self, context, path):
        return AutoIdForDatum(self.datum.in_context(context=context, path=path))

    def __eq__(self, other):
        return isinstance(other, AutoIdForDatum) and other.datum == self.datum and self.id_field == other.id_field


class Root(JSONPath):
    """
    The JSONPath referring to the "root" object. Concrete syntax is '$'.
    The root is the topmost datum without any context attached.
    """

    def find(self, data):
        if not isinstance(data, DatumInContext):
            return [DatumInContext(data, path=Root(), context=None)]
        else:
            if data.context is None:
                return [DatumInContext(data.value, context=None, path=Root())]
            else:
                return Root().find(data.context)

    def update(self, data, val):
        return val

    def __str__(self):
        return '$'

    def __repr__(self):
        return 'Root()'

    def __eq__(self, other):
        return isinstance(other, Root)

class This(JSONPath):
    """
    The JSONPath referring to the current datum. Concrete syntax is '@'.
    """

    def find(self, datum):
        return [DatumInContext.wrap(datum)]

    def update(self, data, val):
        return val

    def __str__(self):
        return '`this`'

    def __repr__(self):
        return 'This()'

    def __eq__(self, other):
        return isinstance(other, This)

class Child(JSONPath):
    """
    JSONPath that first matches the left, then the right.
    Concrete syntax is <left> '.' <right>
    """
    
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def find(self, datum):
        """
        Extra special case: auto ids do not have children,
        so cut it off right now rather than auto id the auto id
        """
        
        return [submatch
                for subdata in self.left.find(datum)
                if not isinstance(subdata, AutoIdForDatum)
                for submatch in self.right.find(subdata)]

    def __eq__(self, other):
        return isinstance(other, Child) and self.left == other.left and self.right == other.right

    def __str__(self):
        return '%s.%s' % (self.left, self.right)

    def __repr__(self):
        return '%s(%r, %r)' % (self.__class__.__name__, self.left, self.right)

class Parent(JSONPath):
    """
    JSONPath that matches the parent node of the current match.
    Will crash if no such parent exists.
    Available via named operator `parent`.
    """

    def find(self, datum):
        datum = DatumInContext.wrap(datum)
        return [datum.context]

    def __eq__(self, other):
        return isinstance(other, Parent)

    def __str__(self):
        return '`parent`'

    def __repr__(self):
        return 'Parent()'
        

class Where(JSONPath):
    """
    JSONPath that first matches the left, and then
    filters for only those nodes that have
    a match on the right.

    WARNING: Subject to change. May want to have "contains"
    or some other better word for it.
    """
    
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def find(self, data):
        return [subdata for subdata in self.left.find(data) if self.right.find(data)]

    def __str__(self):
        return '%s where %s' % (self.left, self.right)

    def __eq__(self, other):
        return isinstance(other, Where) and other.left == self.left and other.right == self.right

class Descendants(JSONPath):
    """
    JSONPath that matches first the left expression then any descendant
    of it which matches the right expression.
    """
    
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def find(self, datum):
        # <left> .. <right> ==> <left> . (<right> | *..<right> | [*]..<right>)
        #
        # With with a wonky caveat that since Slice() has funky coercions
        # we cannot just delegate to that equivalence or we'll hit an 
        # infinite loop. So right here we implement the coercion-free version.

        # Get all left matches into a list
        left_matches = self.left.find(datum)
        if not isinstance(left_matches, list):
            left_matches = [left_matches]

        def match_recursively(datum):
            right_matches = self.right.find(datum)

            # Manually do the * or [*] to avoid coercion and recurse just the right-hand pattern
            if isinstance(datum.value, list):
                recursive_matches = [submatch
                                     for i in range(0, len(datum.value))
                                     for submatch in match_recursively(DatumInContext(datum.value[i], context=datum, path=Index(i)))]

            elif isinstance(datum.value, dict):
                recursive_matches = [submatch
                                     for field in datum.value.keys()
                                     for submatch in match_recursively(DatumInContext(datum.value[field], context=datum, path=Fields(field)))]

            else:
                recursive_matches = []

            return right_matches + list(recursive_matches)
                
        # TODO: repeatable iterator instead of list?
        return [submatch
                for left_match in left_matches
                for submatch in match_recursively(left_match)]
            
    def is_singular():
        return False

    def __str__(self):
        return '%s..%s' % (self.left, self.right)

    def __eq__(self, other):
        return isinstance(other, Descendants) and self.left == other.left and self.right == other.right

class Union(JSONPath):
    """
    JSONPath that returns the union of the results of each match.
    This is pretty shoddily implemented for now. The nicest semantics
    in case of mismatched bits (list vs atomic) is to put
    them all in a list, but I haven't done that yet.

    WARNING: Any appearance of this being the _concatenation_ is
    coincidence. It may even be a bug! (or laziness)
    """
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def is_singular(self):
        return False

    def find(self, data):
        return self.left.find(data) + self.right.find(data)

class Intersect(JSONPath):
    """
    JSONPath for bits that match *both* patterns.

    This can be accomplished a couple of ways. The most
    efficient is to actually build the intersected
    AST as in building a state machine for matching the
    intersection of regular languages. The next
    idea is to build a filtered data and match against
    that.
    """
    def __init__(self, left, right):
        self.left = left
        self.right = right

    def is_singular(self):
        return False

    def find(self, data):
        raise NotImplementedError()

class Fields(JSONPath):
    """
    JSONPath referring to some field of the current object.
    Concrete syntax ix comma-separated field names.

    WARNING: If '*' is any of the field names, then they will
    all be returned.
    """
    
    def __init__(self, *fields):
        self.fields = fields

    def get_field_datum(self, datum, field):
        if field == auto_id_field:
            return AutoIdForDatum(datum)
        else:
            try:
                field_value = datum.value[field] # Do NOT use `val.get(field)` since that confuses None as a value and None due to `get`
                return DatumInContext(value=field_value, path=Fields(field), context=datum)
            except (TypeError, KeyError, AttributeError):
                return None

    def reified_fields(self, datum):
        if '*' not in self.fields:
            return self.fields
        else:
            try:
                fields = tuple(datum.value.keys())
                return fields if auto_id_field is None else fields + (auto_id_field,)
            except AttributeError:
                return ()

    def find(self, datum):
        datum  = DatumInContext.wrap(datum)
        
        return  [field_datum
                 for field_datum in [self.get_field_datum(datum, field) for field in self.reified_fields(datum)]
                 if field_datum is not None]

    def __str__(self):
        return ','.join(self.fields)

    def __repr__(self):
        return '%s(%s)' % (self.__class__.__name__, ','.join(map(repr, self.fields)))

    def __eq__(self, other):
        return isinstance(other, Fields) and tuple(self.fields) == tuple(other.fields)


class Index(JSONPath):
    """
    JSONPath that matches indices of the current datum, or none if not large enough.
    Concrete syntax is brackets. 

    WARNING: If the datum is not long enough, it will not crash but will not match anything.
    NOTE: For the concrete syntax of `[*]`, the abstract syntax is a Slice() with no parameters (equiv to `[:]`
    """

    def __init__(self, index):
        self.index = index

    def find(self, datum):
        datum = DatumInContext.wrap(datum)
        
        if len(datum.value) > self.index:
            return [DatumInContext(datum.value[self.index], path=self, context=datum)]
        else:
            return []

    def __eq__(self, other):
        return isinstance(other, Index) and self.index == other.index

    def __str__(self):
        return '[%i]' % self.index

class Slice(JSONPath):
    """
    JSONPath matching a slice of an array. 

    Because of a mismatch between JSON and XML when schema-unaware,
    this always returns an iterable; if the incoming data
    was not a list, then it returns a one element list _containing_ that
    data.

    Consider these two docs, and their schema-unaware translation to JSON:
    
    <a><b>hello</b></a> ==> {"a": {"b": "hello"}}
    <a><b>hello</b><b>goodbye</b></a> ==> {"a": {"b": ["hello", "goodbye"]}}

    If there were a schema, it would be known that "b" should always be an
    array (unless the schema were wonky, but that is too much to fix here)
    so when querying with JSON if the one writing the JSON knows that it
    should be an array, they can write a slice operator and it will coerce
    a non-array value to an array.

    This may be a bit unfortunate because it would be nice to always have
    an iterator, but dictionaries and other objects may also be iterable,
    so this is the compromise.
    """
    def __init__(self, start=None, end=None, step=None):
        self.start = start
        self.end = end
        self.step = step
    
    def find(self, datum):
        datum = DatumInContext.wrap(datum)
        
        # Here's the hack. If it is a dictionary or some kind of constant,
        # put it in a single-element list
        if (isinstance(datum.value, dict) or isinstance(datum.value, six.integer_types) or isinstance(datum.value, six.string_types)):
            return self.find(DatumInContext([datum.value], path=datum.path, context=datum.context))

        # Some iterators do not support slicing but we can still
        # at least work for '*'
        if self.start == None and self.end == None and self.step == None:
            return [DatumInContext(datum.value[i], path=Index(i), context=datum) for i in xrange(0, len(datum.value))]
        else:
            return [DatumInContext(datum.value[i], path=Index(i), context=datum) for i in range(0, len(datum.value))[self.start:self.end:self.step]]

    def __str__(self):
        if self.start == None and self.end == None and self.step == None:
            return '[*]'
        else:
            return '[%s%s%s]' % (self.start or '', 
                                   ':%d'%self.end if self.end else '',
                                   ':%d'%self.step if self.step else '')

    def __repr__(self):
        return '%s(start=%r,end=%r,step=%r)' % (self.__class__.__name__, self.start, self.end, self.step)

    def __eq__(self, other):
        return isinstance(other, Slice) and other.start == self.start and self.end == other.end and other.step == self.step
