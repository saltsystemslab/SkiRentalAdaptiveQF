import os
import sys
import json
import pandas as pd

def parse_json_file(path):
    jsonObj = {}
    data = ''
    with open(path) as f:
        for l in f.readlines():
            data = data + l

        decoder = json.JSONDecoder()
        while data:
            value, new_start = decoder.raw_decode(data)
            data = data[new_start:].strip()
            jsonObj=value
    return jsonObj



testDir = sys.argv[1]

filters = ['adaptive', 'dSkiAdaptive', 'nonAdaptive', 'sampleDetect']
db_stats = {}
db_stats_summary = []
for filter in filters:
    db_stats[filter] = pd.json_normalize(parse_json_file(os.path.join(testDir, filter + '_db_stats.json')))
    stats_summary = {}
    stats_summary['name'] = filter
    stats_summary['block_manager_blocks_read'] = db_stats[filter]['wiredTiger.block-manager.blocks read'].sum()
    stats_summary['block_manager_bytes_read'] = db_stats[filter]['wiredTiger.block-manager.read'].sum()
    stats_summary['cache_bytes_read'] = db_stats[filter]['wiredTiger.cache.bytes read into cache'].sum()
    stats_summary['cache_pages_requested_from_cache'] = db_stats[filter]['wiredTiger.cache.pages requested from the cache'].sum()
    stats_summary['capacity_bytes_read'] = db_stats[filter]['wiredTiger.capacity.bytes read'].sum()
    stats_summary['connection_read_ios'] = db_stats[filter]['wiredTiger.connection.total read I/Os'].sum()
    db_stats_summary.append(stats_summary)

stats_summary = pd.DataFrame(db_stats_summary)
stats_summary.to_csv('db_stats.csv')
print(stats_summary)

filters = ['adaptive', 'dSkiAdaptive', 'nonAdaptive', 'sampleDetect']
rm_stats = {}
rm_stats_summary = []
for filter in filters:
    rm_stats[filter] = pd.json_normalize(parse_json_file(testDir + '/' + filter + '_rm_stats.json'))
    stats_summary = {}
    stats_summary['name'] = filter
    stats_summary['block_manager_blocks_read'] = rm_stats[filter]['wiredTiger.block-manager.blocks read'].sum()
    stats_summary['block_manager_bytes_read'] = rm_stats[filter]['wiredTiger.block-manager.read'].sum()
    stats_summary['cache_bytes_read'] = rm_stats[filter]['wiredTiger.cache.bytes read into cache'].sum()
    stats_summary['cache_pages_requested_from_cache'] = rm_stats[filter]['wiredTiger.cache.pages requested from the cache'].sum()
    stats_summary['capacity_bytes_read'] = rm_stats[filter]['wiredTiger.capacity.bytes read'].sum()
    stats_summary['connection_read_ios'] = rm_stats[filter]['wiredTiger.connection.total read I/Os'].sum()
    rm_stats_summary.append(stats_summary)

stats_summary = pd.DataFrame(rm_stats_summary)
stats_summary.to_csv('rm_stats.csv')
print(stats_summary)
