"""Analyze Unreal Insights CSV exports; no synthetic measurements or screenshots."""
import argparse
import hashlib
import subprocess
import bisect
import collections
import csv
import json
import math
import re
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNS = ROOT / 'Saved/DormancyRuns'

def stats(values):
    if not values:
        return None
    ordered = sorted(values)
    return dict(n=len(values), mean=statistics.mean(values), p95=ordered[math.ceil(len(values)*.95)-1], maximum=max(values), stddev=statistics.stdev(values) if len(values)>1 else 0)

def union_length(intervals):
    total = 0.
    end = -math.inf
    for a, b in sorted(intervals):
        total += max(0., b-max(a,end))
        end = max(end,b)
    return total

def phase_metrics(folder, phase):
    log = (folder / 'export.log').read_text(encoding='utf-8-sig', errors='replace')
    match = re.search(r"region '"+phase+r"' \[([\d.]+) \.\. ([\d.]+)\]", log)
    if not match:
        raise ValueError('Missing trace region '+phase)
    start, end = map(float, match.groups())
    with (folder / (phase+'.csv')).open(encoding='utf-8-sig', newline='') as stream:
        events = [(r['TimerName'],float(r['StartTime']),float(r['EndTime'])) for r in csv.DictReader(stream)]
    frames = [(a,b) for name,a,b in events if name=='FEngineLoop::Tick' and a>=start and b<=end]
    assert len(frames)>30, (folder, phase, len(frames))
    starts = [a for a,b in frames]
    idle = [[] for _ in frames]
    metrics = {name:[0.]*len(frames) for name in ('replication_ms','prioritize_process_ms','consider_ms','controller_ms','mutation_ms','flush_ms','wake_ms')}
    mapping = {'ServerReplicateActors Time':'replication_ms', 'Process Prioritized Actors Time':'prioritize_process_ms', 'Consider Actors Time':'consider_ms', 'Dormancy_Controller':'controller_ms', 'Dormancy_ChangeBatch':'mutation_ms', 'Resource_Flush':'flush_ms', 'Resource_Wake':'wake_ms'}
    for name,a,b in events:
        i = bisect.bisect_right(starts,a)-1
        if i<0 or i>=len(frames) or b>frames[i][1]+1e-7:
            continue
        if name in ('Game thread idle time','Explicit wait for RHI thread'):
            idle[i].append((a,b))
        if name in mapping:
            metrics[mapping[name]][i] += (b-a)*1000
    metrics['gt_active_ms'] = [(b-a-union_length(idle[i]))*1000 for i,(a,b) in enumerate(frames)]
    metrics['gt_frame_wall_ms'] = [(b-a)*1000 for a,b in frames]
    assert sum(metrics['replication_ms']) > 0, 'Required replication scopes are absent'
    assert min(metrics['gt_active_ms']) >= -1e-6, 'Invalid overlapping idle accounting'
    result = dict(trace_start=start, trace_end=end, complete_frames=len(frames), effective_frame_hz=len(frames)/(frames[-1][1]-frames[0][0]), metrics={k:stats(v) for k,v in metrics.items()})
    # Engine aggregate scopes are supporting attribution, not additive to parent scopes.
    with (folder/(phase+'-stats.csv')).open(encoding='utf-8-sig',newline='') as stream:
        result['engine_scope_totals_seconds']={r['Name']:float(r['Incl']) for r in csv.DictReader(stream) if r['Name'] in ('ServerReplicateActors Time','Process Prioritized Actors Time','Replicate Actor Time','DormancySample','Consider Actors Time','World Tick Time','Tick Time')}
    return result

def analyze(folder):
    text = (folder/'server.log').read_text(encoding='utf-8-sig',errors='replace')
    ids = set(re.findall(r'LAB_EVENT Run=([A-F0-9]+) Initial', text))
    if len(ids) != 1:
        raise ValueError('Use an independent single-policy run for CSV analysis; open comparison traces directly in Insights')
    match = re.search(r'LAB_EVENT Run=([A-F0-9]+) Initial',text)
    if not match:
        raise ValueError('Run ID missing')
    rid=match.group(1)
    raw=ROOT/'Saved/Dormancy'/rid
    manifest=dict(line.split('=',1) for line in (raw/'manifest.txt').read_text(encoding='utf-8-sig').splitlines() if '=' in line)
    result=dict(name=folder.name, run_id=rid, conditions=manifest, final=(raw/'result.txt').read_text(encoding='utf-8-sig'), phases={})
    assert 'CatchupSteps=0' in result['final'], 'Schedule catch-up invalidates frequency comparison'
    assert 'Final state matched' in result['final'] and 'ConnectionChanged=0' in result['final'], 'Invalid performance run'
    result['execution']=json.loads((folder/'execution.json').read_text(encoding='utf-8-sig'))
    assert result['execution']['visual_clients'], 'Headless validation is not a visual benchmark result'
    schedule=(raw/'schedule.txt').read_text(encoding='utf-8-sig')
    assert 'schema=resource-v1' in schedule, 'Not a resource experiment'
    result['schedule_sha256']=hashlib.sha256(schedule.encode()).hexdigest()
    event_rows=list(csv.reader((raw/'events.csv').open(encoding='utf-8-sig')))
    times={r[0]:float(r[2]) for r in event_rows if r[0] in ('Stable','Change','Converge')}
    connections=collections.defaultdict(list)
    for r in csv.reader((raw/'connections.csv').open(encoding='utf-8-sig')):
        connections[r[2]].append((float(r[0]),int(r[3]),int(r[4])))
    for phase, next_phase in [('Stable','Change'),('Change','Converge')]:
        p=phase_metrics(folder,phase)
        p['connections']={}
        for connection,rows in connections.items():
            a=min(rows,key=lambda r:abs(r[0]-times[phase]))
            b=min(rows,key=lambda r:abs(r[0]-times[next_phase]))
            assert abs(a[0]-times[phase])<.04 and abs(b[0]-times[next_phase])<.04
            samples=[r for r in rows if a[0]<=r[0]<=b[0]]
            rates=[(y[1]-x[1])/(y[0]-x[0]) for x,y in zip(samples,samples[1:]) if y[0]-x[0]>.05]
            p['connections'][connection]=dict(bytes=b[1]-a[1],packets=b[2]-a[2],elapsed=b[0]-a[0],bytes_per_second=(b[1]-a[1])/(b[0]-a[0]),sample_rate_stats=stats(rates))
        result['phases'][phase]=p
    (folder/'analysis.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    return result

def export(folder):
    output=folder.as_posix()
    commands=[]
    for phase in ('Stable','Change'):
        commands += [f'TimingInsights.ExportTimingEvents "{output}/{phase}.csv" -columns=ThreadName,TimerName,StartTime,EndTime,Duration,Depth -threads=GameThread -timers="FEngineLoop::Tick,*idle*,*wait*,ServerReplicateActors Time,Process Prioritized Actors Time,*Consider*,Dormancy_*,Resource_*" -region={phase}',
                     f'TimingInsights.ExportTimerStatistics "{output}/{phase}-stats.csv" -threads=GameThread -region={phase}']
    response=folder/'export.rsp'
    response.write_text('\n'.join(commands)+'\n',encoding='utf-8')
    subprocess.run(['G:/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealInsights.exe',
                    f'-OpenTraceFile={folder}/server.utrace','-AutoQuit','-NoUI',
                    f'-ExecOnAnalysisCompleteCmd=@={response}',f'-abslog={folder}/export.log'],check=True)

def main():
    parser=argparse.ArgumentParser(description='Export real Insights traces and summarize visual resource benchmark runs.')
    parser.add_argument('names',nargs='+',help='Run folder names under Saved/DormancyRuns; each trace must contain one policy.')
    parser.add_argument('--reuse-exports',action='store_true')
    args=parser.parse_args()
    destination=ROOT/'Content/DormancyResults'
    destination.mkdir(parents=True,exist_ok=True)
    results=[]
    for name in args.names:
        folder=(RUNS/name).resolve()
        if folder.parent != RUNS.resolve():
            raise ValueError('Expected a direct run folder name')
        if not args.reuse_exports:
            export(folder)
        results.append(analyze(folder))
    groups=collections.defaultdict(list)
    peers=collections.defaultdict(list)
    for run in results:
        c=run['conditions']
        key=tuple(c[k] for k in ('actors','percent','interval','connections','netmode','warm','stable','change'))
        peers[key].append(run)
        groups[key+(c['scenario'],)].append(run)
    for runs in peers.values():
        assert len({r['schedule_sha256'] for r in runs})==1, 'Damage schedules differ'
        for field in ('Digest','Steps','Changes'):
            assert len({re.search('^'+field+'=(.+)$',r['final'],re.M).group(1) for r in runs})==1, f'Unfair comparison: {field}'
        assert len({r['execution']['module_sha256'] for r in runs})==1, 'Different builds in one comparison'
    lines=['RESOURCE DORMANCY / REAL MEASUREMENTS','Source: Unreal Insights timing events and engine UNetConnection counters.',
           'Visible clients on the same host. CPU includes host scheduling contention.',
           'GT active: frame wall time minus instrumented idle/wait union, not OS CPU time.',
           'Packet bytes include engine packet overhead; not application payload or NIC wire bytes.',
           'Phase percentiles use complete frames. Run-to-run deviation is reported separately.',
           'Replication and flush/wake scopes are separate; do not sum nested inclusive scopes.','']
    for key,runs in sorted(groups.items()):
        lines.append(f'Actors {key[0]} | cohort {key[1]}% | interval {key[2]}s | connections {key[3]} | netmode {key[4]} | policy {key[-1]} | repeats {len(runs)}')
        for phase in ('Stable','Change'):
            means=[r['phases'][phase]['metrics']['replication_ms']['mean'] for r in runs]
            summary=stats(means)
            lines.append(f'  {phase}: replication mean {summary["mean"]:.4f} ms/frame; repeat SD {summary["stddev"]:.4f}; range {min(means):.4f}..{max(means):.4f}')
        for run in runs:
            lines.append(f'  Run {run["run_id"]} | UE {run["conditions"]["engine"]} | {run["name"]}')
            for phase,p in run['phases'].items():
                m=p['metrics']; v=m['replication_ms']
                lines.append(f'    {phase} trace {p["trace_start"]:.3f}..{p["trace_end"]:.3f}s: replication avg/p95/max {v["mean"]:.4f}/{v["p95"]:.4f}/{v["maximum"]:.4f} ms; GT active {m["gt_active_ms"]["mean"]:.4f} ms; flush {m["flush_ms"]["mean"]:.4f} ms; wake {m["wake_ms"]["mean"]:.4f} ms')
                for connection,data in p['connections'].items():
                    lines.append(f'      Connection {connection}: {data["bytes_per_second"]/1024:.2f} KiB/s; {data["packets"]} packets in {data["elapsed"]:.2f}s')
            lines.append('    '+run['final'].splitlines()[0])
        lines.append('')
    notes = [
        'HOW TO READ THIS RESULT',
        'Policy 0 = always Awake; 1 = Flush before each damage; 2 = Awake throughout damage interval.',
        'Stable phases isolate unchanged resources. Change phases apply the same fixed cohort and damage schedule.',
        'The Awake stable call tree shows DormancySample under Replicate Actor / Process Prioritized Actors.',
        'The dormant stable call tree removes that repeated resource work; player, controller, lab and connection work remain.',
        'Flush reopens channels: Networking Insights packet 7483 in rare-p1-r1 shows NewActor, Health and Version.',
        'This channel work explains why frequent Flush can cost more than staying Awake through a burst.',
        'These runs do not establish the crossover where Dormancy is slower than always Awake.',
        'Only two repetitions per condition: variation is descriptive, not a confidence interval.',
        'Same Windows host, Ryzen 9 5950X, 64 GB RAM; editor server mode, two rendered clients, 60 FPS cap.',
        'UnrealEditor -server is a separate dedicated process, not a packaged Server-target build.',
        'Existing unrelated editors remained open. Instrumentation and shared-machine contention are included.',
        'Standalone, packaged server and Listen performance: NOT MEASURED.',
        'Late join, 100 ms lag / 5% loss, and Listen final-state convergence passed separate headless correctness runs.',
        'New all-client F/HUD input and evidence-image zoom: pending user validation.',
        'PNG captures are actual Insights screens. Their mouse-selected windows approximate the named phase;',
        'reported numbers use exact trace regions and complete frames from CSV exports.',
        'Raw evidence: Saved/DormancyRuns/<name>/server.utrace and exports; Saved/Dormancy/<Run ID>/.',
        ''
    ]
    lines[8:8] = notes
    (destination/'resource-runs.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
    (destination/'resource-summary.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print('\n'.join(lines))

if __name__=='__main__':
    main()
