#!/usr/bin/env python3
"""Execute the selected study, enforcing a complete medium report before large."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from run import ROOT, validate_artifacts
from report import read_metrics, tex
from paper_report import build_paper_sections

DOCUMENT_ROOT = (ROOT.parents[1] if (ROOT.parents[1] / 'randomized-ski-qf.tex').is_file()
                 else ROOT / 'reports/randomized-skiqf')
FAMILIES = ['distributions', 'adaptive-adversary', 'singleton-mixture']


def update_document(bundles):
    body = []
    for bundle in bundles:
        manifest = json.loads((bundle / 'run.json').read_text())
        if manifest['status'] != 'COMPLETED' or len(manifest['cases']) != 10:
            raise RuntimeError(f'Incomplete stage: {bundle}')
        groups = {}
        body.append('\\subsection*{' + tex(manifest['size'].capitalize()) + ' results}')
        body.append('All ten cases completed. Each measurement is from one run.\\par')
        for case in manifest['cases']:
            if case['status'] != 'COMPLETED':
                raise RuntimeError(f'Failed case: {case["id"]}')
            directory = bundle / case['artifacts']
            config = case['config']
            validate_artifacts(directory, config)
            group = dict(config=config, filters={}, records=[case])
            for name in config['filter_names'].split(','):
                metrics, rounds = read_metrics(directory, name, config)
                group['filters'][name] = [dict(metrics=metrics, rounds=rounds, repetition=1)]
            groups[case['id']] = group
            if config.get('collect_workload_stats'):
                stats = json.loads((directory/'workload_stats.json').read_text())
                hist = (directory/'workload_frequencies.csv').read_text().splitlines()[1:]
                values = [tuple(map(int, line.split(','))) for line in hist]
                if sum(n*c for n,c in values) != config['num_queries'] or sum(c for n,c in values) != stats['distinct_keys']:
                    raise RuntimeError('Invalid workload histogram')
                if config['query_workload'] == 'singleton-mixture':
                    if any(n not in (1,2) for n,c in values) or abs(stats['singleton_key_fraction']-config['singleton_fraction']) > 3/stats['distinct_keys']:
                        raise RuntimeError('Incorrect mixture composition')
                det = group['filters']['skiQF'][0]['metrics']['throughput']
                rand = group['filters']['randomizedSkiQF'][0]['metrics']['throughput']
                baseline = group['filters']['nonAdaptive'][0]['metrics'].get('initial_fp_singleton_fraction')
                body.append(tex(case['id']) + ': singleton keys '
                            + f"{100*stats['singleton_key_fraction']:.2f}\\%, singleton operations {100*stats['singleton_operation_fraction']:.2f}\\%; "
                            + ('initial-FP singleton keys unavailable; ' if baseline is None else f'initial-FP singleton keys {100*baseline:.2f}\\%; ')
                            + f'randomized/deterministic throughput {rand/det:.3f}.\\par')
        plot_body, _ = build_paper_sections(groups, bundle/'paper')
        relative = Path(os.path.relpath(bundle/'paper', DOCUMENT_ROOT)).as_posix()
        body.extend(line.replace('\\input{paper/', '\\input{'+relative+'/') for line in plot_body)
    (DOCUMENT_ROOT/'randomized-experiment-results.tex').write_text('\n'.join(body)+'\n')
    for _ in range(2):
        result = subprocess.run(['pdflatex','-interaction=nonstopmode','-halt-on-error','-output-directory=/tmp','randomized-ski-qf.tex'],cwd=DOCUMENT_ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode:
            raise RuntimeError(result.stdout[-5000:])
    shutil.copyfile('/tmp/randomized-ski-qf.pdf',DOCUMENT_ROOT/'randomized-ski-qf.pdf')


def main():
    bundles = []
    for size in ['medium','large']:
        bundle = ROOT/'sponge/tods'/f'randomized-{size}'
        if bundle.exists() and any(bundle.iterdir()):
            raise RuntimeError(f'Refusing to overwrite existing bundle: {bundle}')
        cmd = [sys.executable,str(ROOT/'experiments/run.py'),'run','--tests',*FAMILIES,'--size',size,'--repetitions','1','--output',str(bundle)]
        print(f'Starting {size}: '+ ' '.join(cmd),flush=True)
        subprocess.run(cmd,cwd=ROOT,check=True)
        bundles.append(bundle)
        update_document(bundles)
        print(f'{size.upper()} GATE PASSED: all cases validated and consolidated PDF built.',flush=True)
        if size == 'medium' and shutil.disk_usage(ROOT).free < 30*1024**3:
            raise RuntimeError('Less than 30 GiB free before large stage; large not started')

if __name__ == '__main__':
    main()
