"""Optional terminal progress from the benchmark's existing execution log."""
import re
import threading
from tqdm import tqdm


class CaseProgress:
    def __init__(self, path, config):
        self.path = path
        self.config = config
        self.filter = ''
        self.phase = ''
        self.bar = None
        self.stop = threading.Event()

    def stage(self, phase, total=None):
        if self.bar is not None:
            self.bar.close()
        self.phase = phase
        self.bar = tqdm(total=total, desc=f'{self.filter} {phase}'.strip(),
                        unit='query', unit_scale=True, mininterval=1,
                        bar_format='{desc}: {percentage:3.0f}% {n_fmt}/{total_fmt} [elapsed {elapsed}]'
                        if total is not None else '{desc}: elapsed {elapsed}')

    def consume(self, line):
        match = re.search(r'Writing to (?:.*/)?([^/\s]+)\.csv', line)
        if match:
            self.filter = match[1]
            self.stage('construction / filter initialization')
        elif 'Sorting keys' in line:
            self.stage('construction / sorting keys')
        elif 'Beginning bulkLoad' in line:
            self.stage('construction / filter bulk load')
        elif 'Loading database' in line:
            self.stage('construction / database load')
        elif 'Done. starting test' in line:
            self.stage('queries', self.config['num_queries'])
        elif 'Done with the test' in line:
            self.stage('finalization / microbenchmarks')
        elif self.phase == 'queries':
            fields = line.split()
            if len(fields) != 10 or not fields[0].isdigit():
                return
            try:
                list(map(float, fields))
            except ValueError:
                return
            completed_rounds = int(fields[0]) + 1
            if not 1 <= completed_rounds <= self.config['num_rounds']:
                return
            completed = max(completed_rounds, self.config['num_queries'] *
                            completed_rounds // self.config['num_rounds'])
            completed = min(completed, self.config['num_queries'])
            self.bar.update(max(0, completed - self.bar.n))

    def watch(self):
        with self.path.open(errors='replace') as log:
            while True:
                for line in log:
                    self.consume(line)
                if self.stop.is_set():
                    break
                self.bar.refresh()
                self.stop.wait(1)

    def __enter__(self):
        self.stage('setup / workload generation / build')
        self.thread = threading.Thread(target=self.watch, daemon=True)
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.stop.set()
        self.thread.join()
        self.bar.close()
