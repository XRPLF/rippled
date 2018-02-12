import os
import json
import sys

VARIANT = os.environ.get('VARIANT', 'release')
EXPECTED_BEHAVIOR = ('OK', 'UNIMPLEMENTED', 'INFORMATIONAL')
EXPECTED_BEHAVIOR_CLOSE = ('OK', 'INFORMATIONAL')
WARNINGS = ("peer did not respond (in time) in closing handshake", )

args = sys.argv[1:]
fn = os.path.abspath(args[0])
indexPath = os.path.dirname(fn)
relativeToIndex = lambda f: os.path.join(indexPath, f)
print "index", fn


failures = []
warnings = []

with open(fn, 'r') as fh:
    index = json.load(fh)
    for servername, serverResults in index.items():
        for test in serverResults:
            result = serverResults[test]
            if ((result['behavior'] not in EXPECTED_BEHAVIOR) or
                 result['behaviorClose'] not in EXPECTED_BEHAVIOR_CLOSE):
                with open(relativeToIndex(result['reportfile'])) as rh:
                    report = json.load(rh)
                    if (report.get('wasNotCleanReason', '') in WARNINGS and
                        VARIANT != 'release'):
                        warnings.append(report)
                    else:
                        failures.append(report)


if warnings:
    print >> sys.stderr, json.dumps(warnings, indent=2)
    print >> sys.stderr, 'there was %s warnings' % len(warnings)

if failures:
    print >> sys.stderr, json.dumps(failures, indent=2)
    print >> sys.stderr, 'there was %s failures' % len(failures)
    sys.exit(1)
