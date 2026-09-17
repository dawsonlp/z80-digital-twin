"""Capture-local graph semantics through the real CLI; no ROM required."""
import copy
import json
import pathlib
import subprocess
import sys
import tempfile


def sample(name, address, data, target, flags=0):
    return dict(id=name, kind='instruction', sequence='0', start=address, next_pc=target,
                cycles='0', read_count=str(len(data)), complete_capture=True,
                bytes=data, revisions=[], before=dict(flags=flags, b=None, hl=None, ix=None, iy=None))


with tempfile.TemporaryDirectory(prefix='z80-graph-') as folder:
    source = pathlib.Path(folder) / 'capture.json'
    capture = dict(format='z80-transfer-capture', version=1, source='graph-test',
                   limitations='Synthetic selected observations, not continuous execution', samples=[])
    command = [sys.argv[1], 'transfers', '--source', str(source), '--through', 'effects']

    def run(data=capture, args=()):
        source.write_text(json.dumps(data))
        return subprocess.run(command + list(args), check=True, capture_output=True).stdout

    def destination(graph, address):
        return next(d for d in graph['destinations'] if d['address'] == address)

    capture['samples'] = [sample(f'loop{i}', 0x8000, [0xCD, 0, 0x90], 0x9000) for i in range(1000)]
    capture['samples'] += [sample('other', 0x8100, [0xCD, 0, 0x90], 0x9000)]
    first = run()
    assert first == run()
    graph = json.loads(first)['transfer_graph']
    callers = destination(graph, 0x9000)['callers']
    assert (callers['site_variant_count'], callers['source_address_count'], callers['occurrence_count']) == (2, 2, 1001)
    assert len(set(callers['samples'])) == 1001
    assert destination(graph, 0x9000)['captured_site_candidates'] == []
    assert graph['counts_complete_for_supplied_capture'] and not graph['destination_sets_closed']
    assert graph['routine_graph_status'] == 'not_analyzed'

    # A changed encoding is another source variant at the same address.
    capture['samples'].append(sample('changed', 0x8000, [0xC4, 0, 0x90], 0x9000))
    new = json.loads(run())['transfer_graph']
    assert destination(new, 0x9000)['callers']['site_variant_count'] == 3
    assert destination(new, 0x9000)['callers']['source_address_count'] == 2
    assert graph['sample_count'] == 1001  # retained prior report remains unchanged

    capture['samples'] = [
        sample('taken', 0x8000, [0xC4, 3, 0x80], 0x8003),
        sample('untaken', 0x8000, [0xC4, 3, 0x80], 0x8003, 0x40),
        sample('unknown', 0x8000, [0xC4, 3, 0x80], 0x8003, None),
        sample('contradiction', 0x8000, [0xC4, 3, 0x80], 0x9000),
        sample('jump1', 0x8200, [0xE9], 0x8300),
        sample('jump2', 0x8200, [0xE9], 0x8400),
        sample('return', 0x8500, [0xC9], 0x8003),
        sample('cycle1', 0x8300, [0xC3, 0, 0x84], 0x8400),
        sample('cycle2', 0x8400, [0xC3, 0, 0x83], 0x8300),
        sample('recursive-call', 0x8600, [0xCD, 0, 0x86], 0x8600),
        sample('rst', 0x8700, [0xFF], 0x38),
    ]
    graph = json.loads(run())['transfer_graph']
    assert destination(graph, 0x8003)['callers']['occurrence_count'] == 1
    assert destination(graph, 0x9000)['callers']['occurrence_count'] == 0
    conditional = next(s for s in graph['sites'] if s['start'] == 0x8000)['conditional_outcomes']
    assert [conditional[k]['count'] for k in ('taken', 'untaken', 'unresolved')] == [1, 1, 2]
    jump = next(s for s in graph['sites'] if s['start'] == 0x8200)
    assert jump['successors']['destination_count'] == 2
    assert destination(graph, 0x8300)['callers']['occurrence_count'] == 0
    assert destination(graph, 0x8600)['callers']['occurrence_count'] == 1
    assert destination(graph, 0x38)['callers']['occurrence_count'] == 1
    assert not destination(graph, 0x8300)['target_variant_established']
    text = run(args=['--format', 'text']).decode()
    assert text == run(args=['--format', 'text']).decode()
    assert '$8000 (site ' in text and 'additional callers' in text and 'Routine graph not analyzed' in text

    # Duplicate evidence identity is rejected, not counted twice.
    bad = copy.deepcopy(capture)
    bad['samples'].append(copy.deepcopy(bad['samples'][0]))
    source.write_text(json.dumps(bad))
    result = subprocess.run(command, capture_output=True)
    assert result.returncode and not result.stdout

    # Incomplete instructions and machine transitions cannot become callers.
    partial = sample('partial', 0x8800, [0xCD], 0x9000)
    partial['complete_capture'] = False
    transition = sample('transition', 0x8800, [], 0x38)
    transition.update(kind='machine_transition', complete_capture=False)
    uncertain = json.loads(run(dict(capture, samples=[partial, transition])))['transfer_graph']
    assert destination(uncertain, 0x9000)['by_category']['unresolved']['occurrence_count'] == 1
    assert destination(uncertain, 0x38)['by_category']['machine_transition']['occurrence_count'] == 1
    assert all(d['callers']['occurrence_count'] == 0 for d in uncertain['destinations'])
    oversized = dict(capture, samples=[sample(str(i), 0x8000, [0], 0x8001) for i in range(8193)])
    source.write_text(json.dumps(oversized))
    result = subprocess.run(command, capture_output=True)
    assert result.returncode and not result.stdout

    # Later continuation resolution changes usage annotations, not raw counts.
    fixture = pathlib.Path(__file__).parent / 'fixtures/analysis/constructed-transfers.json'
    data = json.loads(fixture.read_text())
    effects = json.loads(run(data))['transfer_graph']
    source.write_text(json.dumps(data))
    full_command = command[:-2]
    full = json.loads(subprocess.run(full_command, check=True, capture_output=True).stdout)['transfer_graph']
    assert effects['sites'] == full['sites']
    for old, new in zip(effects['edges'], full['edges'], strict=True):
        assert {k: v for k, v in old.items() if k != 'usage_variants'} == {k: v for k, v in new.items() if k != 'usage_variants'}
    assert any(d['by_continuation_relationship'].get('pushed_target') for d in full['destinations'])
    for target in full['destinations']:
        for relation in target['by_continuation_relationship'].values():
            assert len(relation['samples']) == len(set(relation['samples']))

    assert json.loads(run(dict(capture, samples=[])))['transfer_graph']['sites'] == []
print('PASS: capture-local callers, typed graph, conditional outcomes, usage variants, deterministic text/JSON')
