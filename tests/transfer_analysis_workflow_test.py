"""Fresh-process deterministic reporting; requires no ROM or assembler."""
import json
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="z80-transfer-test-") as folder:
    source = pathlib.Path(folder) / "capture.json"
    capture = {
        "format": "z80-transfer-capture", "version": 1,
        "source": "synthetic-cli-test", "limitations": "No stack history; no claim of hardware fidelity",
        "samples": [{
            "id": "call-1", "kind": "instruction", "sequence": "1",
            "start": 32768, "next_pc": 32771, "cycles": "17", "read_count": "3",
            "complete_capture": True, "bytes": [196, 3, 128], "revisions": [],
            "before": {"flags": 0, "b": None, "hl": None, "ix": None, "iy": None}
        }]
    }
    source.write_text(json.dumps(capture))
    before = source.read_bytes()
    command = [sys.argv[1], "transfers", "--source", str(source)]
    first = subprocess.run(command, check=True, capture_output=True).stdout
    second = subprocess.run(command, check=True, capture_output=True).stdout
    assert first == second and source.read_bytes() == before
    report = json.loads(first)
    assert report["occurrences"][0]["taken"] is True
    assert report["destination_sets_closed"] is False
    capture["samples"][0]["before"]["flags"] = None
    source.write_text(json.dumps(capture))
    report = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    assert report["occurrences"][0]["taken"] is None
    capture["samples"][0]["bytes"] = [256]
    source.write_text(json.dumps(capture))
    invalid = subprocess.run(command, capture_output=True)
    assert invalid.returncode != 0 and not invalid.stdout

    # Real CLI reload of version 2: stack-byte lineage, explanation, then a
    # deliberately broken predecessor. Version 1 coverage above stays mandatory.
    fixture = pathlib.Path(__file__).parent / 'fixtures/analysis/continuations.json'
    capture = json.loads(fixture.read_text())
    source.write_text(json.dumps(capture))
    first = subprocess.run(command, check=True, capture_output=True).stdout
    second = subprocess.run(command, check=True, capture_output=True).stdout
    assert first == second
    report = json.loads(first)
    matched = report['occurrences'][1]['continuation']
    assert matched['status'] == 'matched' and matched['call_sample'] == 'call-outer'
    assert 'call-outer' in matched['explanation']
    capture['samples'][1]['stack']['previous'] = None
    source.write_text(json.dumps(capture))
    report = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    assert report['occurrences'][1]['continuation']['status'] == 'unresolved'
    capture['samples'][1]['stack']['accesses'][0]['kind'] = 'invented'
    source.write_text(json.dumps(capture))
    invalid = subprocess.run(command, capture_output=True)
    assert invalid.returncode != 0 and not invalid.stdout
    fixture = pathlib.Path(__file__).parent / 'fixtures/analysis/value-origins.json'
    source.write_bytes(fixture.read_bytes())
    first = subprocess.run(command, check=True, capture_output=True).stdout
    assert first == subprocess.run(command, check=True, capture_output=True).stdout
    report = json.loads(first)
    assert report['version'] == 6
    jump = report['occurrences'][-1]
    assert jump['value_origin']['status'] == 'traced'
    assert jump['target_basis'] == 'traced_value_origin'
    nodes = {n['id']: n for n in report['value_graph']['nodes']}
    pending = [jump['value_origin']['root']]
    seen = set()
    while pending:
        node = nodes[pending.pop()]
        if node['id'] in seen:
            continue
        seen.add(node['id'])
        pending.extend(node['inputs'])
    assert any(nodes[n]['operation'] == 'call_continuation' and
               nodes[n]['sample_id'] == 'call-outer' for n in seen)
    assert report['destination_sets_closed'] is False
    # Resolution is rerunnable at each stage. Later views preserve the raw
    # earlier findings, while recording exactly which limitations were covered.
    reports = {}
    original = source.read_bytes()
    for stage in ('effects', 'continuations', 'values', 'constructed', 'stack'):
        staged = command + ['--through', stage]
        output = subprocess.run(staged, check=True, capture_output=True).stdout
        assert output == subprocess.run(staged, check=True, capture_output=True).stdout
        reports[stage] = json.loads(output)
        assert reports[stage]['through'] == stage
        assert source.read_bytes() == original
    effects, continuations, values = [reports[s] for s in ('effects', 'continuations', 'values')]
    assert reports['stack'] == report  # Default stage is the latest implemented tactic.
    constructed = reports['constructed']
    last = constructed['occurrences'][-1]
    assert last['constructed_transfer']['pattern'] == 'popped_continuation_jump'
    assert last['constructed_transfer']['return_role_established'] is True
    assert 'register-mediated return' in last['resolution']['comment']
    assert 'Logical call/return role is not established' not in last['resolution']['comment']
    for early, late in zip(values['occurrences'], constructed['occurrences']):
        assert early['constructed_transfer'] is None
        for field in ('instruction_effect', 'continuation', 'value_origin'):
            assert early[field] == late[field]
    assert len({r['capture_sha256'] for r in reports.values()}) == 1
    assert effects['value_graph'] is None and continuations['value_graph'] is None
    for early, middle, late in zip(effects['occurrences'], continuations['occurrences'], values['occurrences']):
        assert early['instruction_effect'] == middle['instruction_effect'] == late['instruction_effect']
        assert early['continuation'] is None and early['value_origin'] is None
        assert middle['continuation'] == late['continuation']
        assert middle['value_origin'] is None
    old_reason = 'popped value provenance into registers is not yet tracked'
    assert old_reason in continuations['occurrences'][1]['resolution']['unresolved']
    pop = values['occurrences'][1]
    assert old_reason in pop['continuation']['unresolved']
    assert old_reason not in pop['resolution']['unresolved']
    assert any(d['reason'] == old_reason and d['resolved_by'] == 'z80-address-values/2'
               for d in pop['resolution']['resolved_dependencies'])
    assert 'call-outer' in values['occurrences'][-1]['resolution']['comment']
    assert 'Logical call/return role is not established' in values['occurrences'][-1]['resolution']['comment']

    # Contradiction must not be hidden merely because a later tactic ran.
    capture = json.loads(original)
    capture['samples'][1]['stack']['accesses'][0]['value'] = 4
    source.write_text(json.dumps(capture))
    broken = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    pop = broken['occurrences'][1]
    assert pop['value_origin']['status'] == 'unresolved'
    assert old_reason in pop['resolution']['unresolved']
    assert any('contradict' in r for r in pop['resolution']['unresolved'])
    assert not pop['resolution']['resolved_dependencies']

    # A later observation at the same instruction can have a different target.
    # Earlier occurrence results remain intact; both site variants stay visible.
    capture = json.loads(original)
    jump_sample = capture['samples'][-1]
    later = json.loads(json.dumps(jump_sample))
    later['id'] = 'later-jump'
    later['before']['hl'] = later['next_pc'] = 0x9000
    later['stack']['previous'] = None
    capture['samples'].append(later)
    source.write_text(json.dumps(capture))
    expanded = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    assert expanded['occurrences'][:-1] == reports['stack']['occurrences']
    site = next(s for s in expanded['sites'] if s['start'] == jump_sample['start'])
    assert len(site['variants']) == 2
    assert {i for v in site['variants'] for i in v['samples']} == {'jump-hl', 'later-jump'}
    assert '$8003' in site['comment'] and '$9000' in site['comment']
    assert site['destination_sets_closed'] is False
    assert 'predating this trace' in expanded['occurrences'][-1]['resolution']['comment']

    # Changed bytes at the same address retain a separate presentation identity.
    changed = json.loads(json.dumps(later))
    changed['id'] = 'changed-code'
    changed['bytes'] = [0x00]
    changed['next_pc'] = changed['start'] + 1
    capture['samples'].append(changed)
    source.write_text(json.dumps(capture))
    changed_report = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    assert len([s for s in changed_report['sites'] if s['start'] == jump_sample['start']]) == 2
    examples = pathlib.Path(__file__).parent / 'fixtures/analysis/constructed-transfers.json'
    example_report = json.loads(subprocess.run([sys.argv[1], 'transfers', '--source', str(examples)],
                                               check=True, capture_output=True).stdout)
    by_id = {o['sample_id']: o for o in example_report['occurrences']}
    assert by_id['jump-hl']['constructed_transfer']['pattern'] == 'popped_continuation_jump'
    assert by_id['jump-hl']['completeness']['continuation'] == 'matched_register_continuation'
    assert by_id['dispatch-ret']['constructed_transfer']['pattern'] == 'pushed_target_ret'
    assert by_id['dispatch-ret']['completeness']['continuation'] == 'pushed_target'
    assert by_id['helper-jump']['constructed_transfer']['pattern'] == 'continuation_preserving_indirect_jump'
    assert by_id['helper-jump']['completeness']['continuation'] == 'preserved_call_continuation'
    assert by_id['helper-return']['continuation']['status'] == 'matched'

    stack_examples = pathlib.Path(__file__).parent / 'fixtures/analysis/stack-reconstruction.json'
    stack_command = [sys.argv[1], 'transfers', '--source', str(stack_examples)]
    encoded = subprocess.run(stack_command, check=True, capture_output=True).stdout
    assert encoded == subprocess.run(stack_command, check=True, capture_output=True).stdout
    stack_report = json.loads(encoded)
    previous_stage = json.loads(subprocess.run(stack_command + ['--through', 'constructed'], check=True, capture_output=True).stdout)
    assert stack_report['capture_sha256'] == previous_stage['capture_sha256']
    for early, late in zip(previous_stage['occurrences'], stack_report['occurrences']):
        assert early['stack_reconstruction'] is None
        for field in ('instruction_effect', 'continuation', 'value_origin', 'constructed_transfer'):
            assert early[field] == late[field]
    by_id = {o['sample_id']: o for o in stack_report['occurrences']}
    skip = by_id['skip-caller']['stack_reconstruction']
    assert skip['pattern'] == 'caller_skipping_exit' and skip['skipped_calls'] == ['inner-call']
    assert skip['supporting_samples'] == ['remove-inner']
    assert by_id['restore-sp']['stack_reconstruction']['pattern'] == 'restored_stack_pointer'
    assert not by_id['restore-sp']['resolution']['unresolved']
    assert by_id['restore-sp']['continuation']['unresolved']  # Raw limitation retained.
    assert by_id['dispatch']['stack_reconstruction']['pattern'] == 'prepared_continuation_candidate'
    assert by_id['consume-prepared']['stack_reconstruction']['pattern'] == 'constructed_continuation_consumed'
    assert by_id['consume-prepared']['resolution']['continuation_relationship'] == 'constructed_continuation_consumed'
    assert not by_id['consume-prepared']['resolution']['unresolved']
    assert by_id['use-replacement']['stack_reconstruction']['pattern'] == 'substituted_continuation_transfer'
    assert by_id['use-replacement']['stack_reconstruction']['return_role_established'] is False

    # Presentation can hit its own bound while value analysis remains complete.
    # It must disclose truncation and preserve the underlying graph.
    capture = json.loads(original)
    capture['samples'] = []
    pc = 0x8000
    program = [[0x21, 0x00, 0x90]] + [[0x23]] * 1100 + [[0xE9]]
    for index, code in enumerate(program):
        sample = json.loads(json.dumps(jump_sample))
        sample.update(id=f'bounded-{index}', start=pc, bytes=code,
                      next_pc=0x9000 + 1100 if index == len(program) - 1 else pc + len(code),
                      read_count=str(len(code)), sequence=str(index))
        sample['before'] = dict(flags=None, b=None, hl=None, ix=None, iy=None)
        sample['stack']['previous'] = capture['samples'][-1]['id'] if capture['samples'] else None
        capture['samples'].append(sample)
        pc = sample['next_pc']
    source.write_text(json.dumps(capture))
    bounded = json.loads(subprocess.run(command, check=True, capture_output=True).stdout)
    assert bounded['occurrences'][-1]['value_origin']['status'] == 'traced'
    assert not bounded['value_graph']['exhausted']
    assert any('summary budget exhausted' in r for r in bounded['occurrences'][-1]['resolution']['unresolved'])
    for args in (['--through', 'invented'], ['--through'], ['--source', str(source)]):
        invalid = subprocess.run(command + args, capture_output=True)
        assert invalid.returncode != 0 and not invalid.stdout
print("PASS: read-only transfer reporting across fresh processes")
