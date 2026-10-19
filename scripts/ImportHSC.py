"""Convert actual saved HSC FEM/ABD snapshots into portable BaguaDT recordings.

Reads the reproduction repository without modifying it or invoking its solver.
Requires Python + NumPy only; the resulting project needs neither at runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zlib
import numpy as np


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_obj(path):
    vertices, faces = [], []
    for line in path.read_text().splitlines():
        w = line.split()
        if not w:
            continue
        if w[0] == 'v':
            vertices.append([float(x) for x in w[1:4]])
        elif w[0] == 'f':
            ids = [int(x.split('/')[0]) for x in w[1:]]
            ids = [i - 1 if i > 0 else len(vertices) + i for i in ids]
            faces += [[ids[0], ids[i], ids[i + 1]] for i in range(1, len(ids) - 1)]
    return np.asarray(vertices), np.asarray(faces, dtype=np.uint32)


def yup(points):
    p = np.asarray(points)
    return np.stack((p[..., 0], p[..., 2], -p[..., 1]), axis=-1)


def convert(source, output, case):
    root = source / 'result/vehicle_full_pass' / case
    manifest = json.loads((root / 'scene_manifest.json').read_text(encoding='utf-8'))
    status = json.loads((root / 'status.json').read_text(encoding='utf-8'))
    if status['status'] != 'COMPLETED_FULL_PASS':
        raise ValueError(f'{case}: not a completed run')
    with np.load(root / 'geometry.npz', allow_pickle=False) as saved:
        geometry = dict(saved)
    files = [p for p in sorted((root / 'frames').glob('frame_*.npz'))
             if int(p.stem.split('_')[-1]) <= status['completed_frames']]
    if not files or int(files[0].stem.split('_')[-1]) != 0:
        raise ValueError('Missing initial frame')
    chassis = source / 'reproduction/vehicle_research/upstream/projectchrono__chrono/data/vehicle/hmmwv/hmmwv_chassis_simple.obj'
    body_geometry = []
    for i, body in enumerate(manifest['bodies']):
        body_geometry.append(read_obj(chassis) if body['name'] == 'chassis'
                             else (geometry[f'body_v_{i}'], geometry[f'body_f_{i}']))

    def world_meshes(snapshot):
        bodies = [yup(v @ matrix[:3, :3].T + matrix[:3, 3])
                  for (v, _), matrix in zip(body_geometry, snapshot['body_transforms'])]
        return bodies + [yup(v) for v in snapshot['tire_vertices']]

    with np.load(files[0], allow_pickle=False) as first:
        initial_points = world_meshes(first)
    names = [b['name'] for b in manifest['bodies']] + [w['name'] + '_tire' for w in manifest['wheels']]
    faces = [f for _, f in body_geometry] + [geometry['tire_faces']] * len(manifest['wheels'])
    colors = ['#698467' if n == 'chassis' else '#c0955c' if n.endswith('_rim') else '#b1c5d1'
              if n.startswith('spindle') else '#a87038' if n.startswith('loose') else '#353e46' for n in names]
    objects, assets, normalizers = [], {}, []
    for i, (name, points, triangles, color) in enumerate(zip(names, initial_points, faces, colors)):
        center = (points.min(axis=0) + points.max(axis=0)) / 2
        extent = np.maximum(points.max(axis=0) - points.min(axis=0), .001)
        normalized = np.asarray((points - center) / extent, dtype=np.float32)
        asset_id = f'hsc-{case}-{i}'
        assets[asset_id] = {'id': asset_id, 'name': name, 'source': f'HSC/{case}/geometry.npz + frames',
                            'sha256': sha(root / 'geometry.npz'), 'positions': normalized.tolist(),
                            'indices': np.asarray(triangles, dtype=np.uint32).ravel().tolist()}
        objects.append({'id': name, 'name': name, 'shape': 'mesh', 'mesh': asset_id,
                        'position': center.tolist(), 'rotation': [0, 0, 0], 'scale': [1, 1, 1],
                        'dimensions': extent.tolist(), 'color': color, 'visible': True,
                        'collisionEnabled': False, 'fixed': True, 'mass': 1})
        normalizers.append((center, extent))
    objects.append({'id': 'ground', 'name': '地面', 'shape': 'plane', 'position': [2, -.002, 0],
                    'rotation': [0, 0, 0], 'scale': [1, 1, 1], 'dimensions': [24, 1, 8],
                    'color': '#d8dfe5', 'visible': True, 'collisionEnabled': False, 'fixed': True, 'mass': 1})
    scene = {'format': 'baguadt.scene', 'version': 1, 'units': 'm', 'upAxis': 'Y', 'selected': 'chassis',
             'view': {'target': [0, .7, 0], 'yaw': 18, 'pitch': 24, 'distance': 13, 'grid': True},
             'objects': objects, 'assets': assets}
    metadata = {'producer': 'HSC reproduction · saved native HSC FEM/ABD run', 'case': case,
                'recomputed_in_BaguaDT': False, 'source_repository': str(source.resolve()),
                'solver': status['solver'], 'status': status['status'], 'completed_steps': status['completed_frames'],
                'simulation_time_s': status['simulation_time_s'], 'source_dt': manifest['config']['dt'],
                'ground_Young_modulus_Pa': manifest['materials']['loose_E'],
                'coordinate_conversion': '(x,y,z) Z-up -> (x,z,-y) Y-up, metres',
                'representation': 'All ABD affine surface motion + FEM tire surface vertex deformation; linear interpolation between saved snapshots',
                'scope': 'Chrono-derived replacement vehicle; not paper-original assets or a claim of paper performance reproduction',
                'source_hashes': {p.name: sha(p) for p in [root/'geometry.npz', root/'scene_manifest.json', root/'status.json', root/'provenance.json', chassis]},
                'snapshots': [{'file': p.name, 'sha256': sha(p)} for p in files]}
    header = json.dumps({'format': 'baguadt.recording', 'version': 1, 'scene': scene, 'metadata': metadata},
                        ensure_ascii=False, separators=(',', ':'), allow_nan=False).encode('utf-8')
    path = output / f'{case}.brec'
    temporary = path.with_suffix('.tmp')
    entries, checks = [], []
    with temporary.open('wb') as stream:
        stream.write(b'BGREC001' + struct.pack('<I', len(header)) + header + struct.pack('<I', len(files)))
        table = stream.tell()
        stream.write(b'\0' * (len(files) * 20))
        for frame_index, file in enumerate(files):
            number = int(file.stem.split('_')[-1])
            with np.load(file, allow_pickle=False) as snapshot:
                points = world_meshes(snapshot)
            raw = bytearray(struct.pack('<I', len(objects)))
            for i, o in enumerate(objects):
                name = o['id'].encode('utf-8')
                raw += struct.pack('<I', len(name)) + name
                transform = o['position'] + o['rotation'] + o['scale'] + o['dimensions']
                count = len(points[i]) if i < len(points) else 0
                raw += struct.pack('<12fBI', *transform, 1, count)
                if count:
                    center, extent = normalizers[i]
                    raw += np.asarray((points[i] - center) / extent, dtype='<f4').tobytes()
            chunk = struct.pack('>I', len(raw)) + zlib.compress(raw, 3)
            entries.append((number * manifest['config']['dt'], stream.tell(), len(chunk)))
            stream.write(chunk)
            if frame_index in (0, len(files)//2, len(files)-1):
                checks.append({'frame_index': frame_index, 'time': entries[-1][0],
                               'chassis_vertex_0': points[0][0].tolist(),
                               'tire_vertex_100': points[len(body_geometry)][100].tolist()})
        stream.seek(table)
        for entry in entries:
            stream.write(struct.pack('<dQI', *entry))
    temporary.replace(path)
    evidence = output / 'provenance' / case
    evidence.mkdir(parents=True, exist_ok=True)
    for name in ('status.json', 'scene_manifest.json', 'provenance.json', 'parameters.json', 'frame_stats.jsonl', 'trajectory.jsonl', 'hsc_stats.jsonl'):
        shutil.copy2(root/name, evidence/name)
    return {'case': case, 'recording': path.name, 'saved_frames': len(files), 'duration': entries[-1][0],
            'objects': len(objects), 'surface_vertices': sum(len(p) for p in initial_points),
            'recording_sha256': sha(path), 'checks': checks}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('D:/Collection/HSC_reimpl'))
    parser.add_argument('--output', type=Path, default=Path(__file__).resolve().parents[1]/'examples/HSC')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    validation = [convert(args.source, args.output, case) for case in ('ground_1e8', 'ground_1e6')]
    project = {'format': 'baguadt.project', 'version': 1, 'name': 'HSC 车辆越障复现 · 完整运行结果',
               'scenes': [{'name': f"HSC · 地面胎 E={case[-3:]} Pa", 'recording': f'{case}.brec'}
                          for case in ('ground_1e8', 'ground_1e6')]}
    (args.output/'HSC.bagua.project.json').write_text(json.dumps(project, indent=2, ensure_ascii=False), encoding='utf-8')
    (args.output/'validation.json').write_text(json.dumps(validation, indent=2), encoding='utf-8')
    notices = args.source/'reproduction/vehicle_research'
    shutil.copy2(notices/'THIRD_PARTY_NOTICES.md', args.output/'SOURCE_NOTICES.md')
    shutil.copy2(notices/'upstream/projectchrono__chrono/LICENSE', args.output/'Chrono-ASSETS-LICENSE')
    shutil.copy2(args.source/'result/vehicle_full_pass/report.md', args.output/'original_report.md')
    print(json.dumps([{k: v for k, v in item.items() if k != 'checks'} for item in validation], indent=2))


if __name__ == '__main__':
    main()
