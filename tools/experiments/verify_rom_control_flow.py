"""Check bounded ROM laboratory observations; optionally compare a fresh replay."""
import json
import pathlib
import sys

folder = pathlib.Path(sys.argv[1])
assert 'BUDGET EXHAUSTED' not in (folder / 'manifest.txt').read_text()

def capture(name):
    return json.loads((folder / (name + '.capture.json')).read_text())['samples']

def report(name):
    return json.loads((folder / (name + '.report.json')).read_text())

def targets(name, pc):
    return [s['next_pc'] for s in capture(name) if s['start'] == pc]

assert len(list(folder.glob('*.capture.json'))) == 16
assert targets('unstack-runtime', 0x1FC8) == [0x1FF8]
assert targets('unstack-syntax', 0x1FC7) == [0x8003]
assert report('unstack-runtime')['occurrences'][-1]['constructed_transfer']['pattern'] == 'popped_continuation_jump'
assert report('unstack-syntax')['occurrences'][-1]['stack_reconstruction']['pattern'] == 'caller_skipping_exit'
for low in range(4):
    for site in (0x03F0, 0x03F4):
        assert targets(f'beeper-l{low}', site) == [0x03D4 - low]
for case, target in [('channel-print', 0x09F4), ('channel-input', 0x10A8),
                     ('channel-select-k', 0x1634), ('channel-select-s', 0x1642)]:
    assert targets(case, 0x162C) == [target]
for case in ('channel-select-k', 'channel-select-s'):
    assert targets(case, 0x1646) == [0x164A]
    assert targets(case, 0x164A) == [0x0D4D]
    sample_id = next(s['id'] for s in capture(case) if s['start'] == 0x162C)
    occurrence = next(o for o in report(case)['occurrences'] if o['sample_id'] == sample_id)
    assert occurrence['constructed_transfer']['pattern'] == 'continuation_preserving_indirect_jump'
    assert occurrence['value_origin']['status'] == 'partial'
assert targets('usr-tail', 0x34BB) == [0x9000]
assert targets('usr-tail', 0x9000) == [0x2D2B]
assert report('usr-tail')['occurrences'][-1]['stack_reconstruction']['pattern'] == 'constructed_continuation_consumed'
for offset, expected in [(0, [0x368F]), (2, [0x343C]), (4, [0x33A1, 0x3365])]:
    assert targets(f'calculator-offset{offset}', 0x33A1) == expected
site = next(s for s in report('calculator-offset4')['sites'] if s['start'] == 0x33A1)
assert len(site['variants']) == 2 and not site['destination_sets_closed']
assert targets('error-stack', 0x005C) == [0x16C5]
assert targets('clear-tail', 0x1EEC) == [0x8003]
assert capture('clear-tail')[-1]['stack']['after_sp'] == 0x8FFE
for case in ('channel-print', 'channel-input', 'channel-select-k', 'channel-select-s'):
    graph = report(case)['transfer_graph']
    entry = next(d for d in graph['destinations'] if d['address'] == 0x162C)
    selected = case.startswith('channel-select-')
    assert entry['callers']['occurrence_count'] == (0 if selected else 1)
    assert entry['by_category']['fallthrough' if selected else 'call']['occurrence_count'] == 1
graph = report('calculator-offset4')['transfer_graph']
ret = next(s for s in graph['sites'] if s['start'] == 0x33A1)
assert ret['successors']['occurrence_count'] == 2 and ret['successors']['destination_count'] == 2
assert next(d for d in graph['destinations'] if d['address'] == 0x33A1)['callers']['occurrence_count'] == 0
if len(sys.argv) > 2:
    replay = pathlib.Path(sys.argv[2])
    assert {p.name for p in folder.iterdir()} == {p.name for p in replay.iterdir()}
    for path in folder.iterdir():
        assert path.read_bytes() == (replay / path.name).read_bytes(), path.name
print('PASS: 16 bounded ROM cases, concrete targets, shared sites, and optional byte-identical replay')
