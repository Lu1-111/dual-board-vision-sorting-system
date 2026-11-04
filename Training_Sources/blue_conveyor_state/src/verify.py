"""Verify dataset/model integrity and all active metadata paths using stdlib."""
import hashlib
import json
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def check_file(path, expected_hash=None):
    assert path.is_file(), f'Missing file: {path}'
    if expected_hash:
        assert hashlib.sha256(path.read_bytes()).hexdigest()==expected_hash, f'Hash mismatch: {path}'


def main():
    data = ROOT/'data'
    rows = json.loads((data/'manifest.json').read_text())
    assert len(rows)==331 and len({r['sample_id'] for r in rows})==331
    for row in rows:
        assert row['review_status']=='approved'
        check_file(data/row['path'], row['sha256'])
        check_file(data/row['prompt_file'])
    train = {r['near_duplicate_group'] for r in rows if r['split']=='train'}
    val = {r['near_duplicate_group'] for r in rows if r['split']=='internal_val'}
    assert not train & val
    assert Counter(r['split'] for r in rows)=={'train':264,'internal_val':67}
    refs = json.loads((data/'reference/manifest.json').read_text())['samples']
    assert len(refs)==7
    for row in refs:
        check_file(data/'reference'/row['path'], row['sha256'])
        check_file(data/'reference'/row['prompt_file'])
    tests = json.loads((data/'test/manifest.json').read_text())
    frozen = json.loads((ROOT/'reports/initial_training/frozen_model.json').read_text())
    assert len(tests)==20 and sum(r['suite']=='classification' for r in tests)==18
    for row in tests:
        check_file(data/'test'/row['path'], row['sha256'])
        for ref in row['reference_paths']:
            check_file(ROOT/ref)
        assert row['generated_unix_time'] > frozen['frozen_unix_time']
    assert not {r['sha256'] for r in rows+refs} & {r['sha256'] for r in tests}
    known_hashes = {r['sha256'] for r in rows+refs+tests}
    registry_file = ROOT/'models/revisions.json'
    registry = json.loads(registry_file.read_text()) if registry_file.exists() else None
    new_test_count = 0
    for manifest_path in sorted((data/'test_batches').glob('*/manifest.json')):
        batch = manifest_path.parent
        plan = json.loads((batch/'plan.json').read_text())
        planned = {r['id']:r for r in plan['tests']}
        samples = json.loads(manifest_path.read_text())
        assert len(samples)==len(planned)
        assert {r['id'] for r in samples}==set(planned)
        for row in samples:
            assert row['review_status']=='accepted' and row['reviewed_before_inference']
            assert row['expected_class']==planned[row['id']]['expected_class']
            assert row['suite']==planned[row['id']]['suite']
            check_file(batch/row['path'], row['sha256'])
            for ref in row['reference_paths']:
                check_file(ROOT/ref)
            assert row['generated_unix_time'] > frozen['frozen_unix_time']
            assert row['sha256'] not in known_hashes
            known_hashes.add(row['sha256'])
        if registry:
            matches = [r for r in registry['revisions'].values() if r['model_hashes']==plan['model_hashes']]
            assert len(matches)==1, f'Unknown model revision for {batch.name}'
            assert all(r['generated_unix_time']>matches[0]['frozen_unix_time'] for r in samples)
        else:
            for filename, sha in plan['model_hashes'].items():
                check_file(ROOT/'models'/filename, sha)
        new_test_count += len(samples)
    for filename, sha in json.loads((ROOT/'models/artifact_hashes.json').read_text()).items():
        check_file(ROOT/'models'/filename, sha)
    if registry:
        active = registry['revisions'][registry['active']]
        assert active['model_hashes']==json.loads((ROOT/'models/artifact_hashes.json').read_text())
        export = json.loads((ROOT/active['export_report']).read_text())
        assert export['checkpoint_sha256']==active['model_hashes']['best.pt']
        assert set(export['calibration_sample_ids']) <= {r['sample_id'] for r in rows if r['split']=='train'}
    calibration = set(json.loads((ROOT/'reports/initial_training/calibration_samples.json').read_text()))
    assert len(calibration)==120
    assert calibration <= {r['sample_id'] for r in rows if r['split']=='train'}
    print(f'Verified: 331 approved images (264 train / 67 internal val), 7 references, 20 original tests, {new_test_count} additional tests; model hashes unchanged.')


if __name__ == '__main__':
    main()
