"""Import the supplied Chrono-derived HSC vehicle as an editable, runnable scene.

Only NumPy and the source files are read; no external simulator is imported or run.
Coordinates are converted from Z-up to Y-up by a proper rotation (x,z,-y).
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def yup(points):
    p = np.asarray(points)
    return np.stack((p[..., 0], p[..., 2], -p[..., 1]), axis=-1)


def convert(source, output, case):
    root = source / 'result/vehicle_full_pass' / case
    manifest_path = root / 'scene_manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    geometry_path = root / 'geometry.npz'
    tire_path = source / 'reproduction/vehicle_research/prepared/tire_tet4.npz'
    recipe_path = source / 'reproduction/vehicle_assembly/scene.py'
    config_path = source / 'reproduction/experiments/vehicle_full_pass/config.json'
    with np.load(geometry_path, allow_pickle=False) as archive:
        geometry = dict(archive)
    with np.load(tire_path, allow_pickle=False) as archive:
        tire = dict(archive)
    objects, assets, links = [], {}, []
    masses = {b['name']: b['mass_kg'] for b in manifest['bodies']}
    transforms = {b['name']: np.asarray(b['transform']) for b in manifest['bodies']}
    matrix = np.array([[1, 0, 0], [0, 0, 1], [0, -1, 0]])

    def add(name, vertices, faces, position, rotation, mass, anchors=(), tets=None, young=None):
        asset = {'id': name, 'name': name, 'source': 'HSC vehicle replacement / ' + case,
                 'sha256': digest(geometry_path), 'positions': yup(vertices).tolist(),
                 'indices': np.asarray(faces).ravel().tolist(),
                 'tetrahedra': [] if tets is None else np.asarray(tets).ravel().tolist()}
        assets[name] = asset
        obj = {'id': name, 'name': name, 'shape': 'mesh', 'mesh': name,
               'position': yup(position).tolist(), 'rotation': rotation, 'scale': [1, 1, 1],
               'dimensions': [1, 1, 1], 'color': '#467fad' if name == 'chassis' else
               '#303740' if tets is not None else '#bd9b62',
               'visible': True, 'fixed': False, 'mass': mass, 'collisionEnabled': True,
               'anchors': yup(np.asarray(anchors).reshape(-1, 3)).tolist()}
        if tets is not None:
            obj['fem'] = {'youngModulus': young, 'poissonRatio': manifest['materials']['poisson'],
                          'resolution': 2, 'pinBottom': False}
        objects.append(obj)

    for i, body in enumerate(manifest['bodies']):
        name, count = body['name'], body['virtual_anchors']
        vertices = geometry[f'body_v_{i}']
        actual = len(vertices) - count
        transform = transforms[name]
        rotated = matrix @ transform[:3, :3] @ matrix.T
        if np.allclose(rotated, np.eye(3)):
            angles = [0, 0, 0]
        elif np.allclose(rotated, [[1, 0, 0], [0, 0, -1], [0, 1, 0]]):
            angles = [90, 0, 0]
        else:
            raise ValueError(f'{name}: unsupported source pose; refuse an approximate conversion')
        add(name, vertices[:actual], geometry[f'body_f_{i}'], transform[:3, 3], angles,
            body['mass_kg'], vertices[actual:])

    for wheel in manifest['wheels']:
        name = wheel['name']
        transform = transforms[name + '_rim']
        # Keep tetrahedral vertices in local space; Object's pose performs the world transform.
        rotation = [90, 0, 0] if name.startswith('loose_') else [0, 0, 0]
        add(name + '_tire', tire['vertices'], tire['surface_triangles'], transform[:3, 3],
            rotation, wheel['tire_mass_kg'], tets=tire['tetrahedra'],
            young=manifest['materials']['loose_E' if name.startswith('loose_') else 'mounted_E'])
        links.append({'id': name + '_bead', 'type': 'stitch', 'a': name + '_rim',
                      'b': name + '_tire', 'pairs': [[i, int(node)] for i, node in enumerate(tire['BC_CONN'])],
                      'stiffness': manifest['materials']['bead_k'], 'rest': 0})

    chassis_position = transforms['chassis'][:3, 3]
    for i, suspension in enumerate(manifest['suspensions']):
        spindle, rim = 'spindle_' + str(i), 'wheel_' + str(i) + '_rim'
        center = transforms[spindle][:3, 3]
        # Original create_geometry uses world-space p,p+0.2Z on BOTH bodies.
        links.append({'id': f'suspension_{i}_guide', 'type': 'prismatic', 'a': 'chassis',
                      'b': spindle, 'anchorA': yup(center - chassis_position).tolist(),
                      'anchorB': [0, 0, 0], 'axis': [0, 1, 0], 'strength': 1e5,
                      'limit': {'lower': -.15, 'upper': .15, 'strength': 100},
                      'damper': {'coefficient': suspension['damping_N_s_m']}})
        links.append({'id': f'suspension_{i}_spring', 'type': 'stitch', 'a': 'chassis',
                      'b': spindle, 'anchorPairs': True, 'pairs': [[i, 0]],
                      'stiffness': suspension['spring_k_N_m'], 'rest': suspension['spring_rest_m']})
        links.append({'id': f'suspension_{i}_motor', 'type': 'revolute', 'a': spindle, 'b': rim,
                      'anchorA': [0, 0, .1], 'anchorB': [0, 0, .1], 'axis': [0, 0, -1],
                      'strength': 1e5, 'drive': {'speed': suspension['motor_rad_s'],
                                               'ramp': .5, 'strength': 1e4}})

    objects.insert(0, {'id': 'ground', 'name': '地面（原生无限平面）', 'shape': 'plane',
                      'position': [0, 0, 0], 'rotation': [0, 0, 0], 'scale': [1, 1, 1],
                      'dimensions': [25, 1, 8], 'color': '#d8dfe5', 'visible': True,
                      'fixed': True, 'collisionEnabled': True, 'mass': 1})
    scene = {'format': 'baguadt.scene', 'version': 1, 'units': 'm', 'upAxis': 'Y',
             'selected': 'chassis', 'dt': manifest['config']['dt'], 'objects': objects,
             'assets': assets, 'links': links, 'exclusions': manifest['contact_exclusions'],
             'view': {'target': [-.5, .6, 0], 'distance': 11, 'yaw': 35, 'pitch': 24, 'grid': True},
             'simulation': {'gravity': 9.8, 'contactFriction': .6, 'contactStiffness': 1e10,
                            'contactDistance': .001, 'abdStiffness': 1e9, 'infiniteGround': True,
                            'newtonIterations': 400, 'lineSearchIterations': 32,
                            'velocityTolerance': .003, 'maxRank': 256, 'maxIterations': 12000,
                            'neumannTerms': 5, 'omega': .7, 'linearTolerance': 1e-4,
                            'adaptiveInverse': True}}
    output.mkdir(parents=True, exist_ok=True)
    target = output / (case + '.bagua.json')
    target.write_text(json.dumps(scene, separators=(',', ':'), ensure_ascii=False), encoding='utf-8')
    provenance = {'case': case, 'kind': manifest['model'], 'source_sha256': {
        str(p.relative_to(source)): digest(p) for p in
        (manifest_path, geometry_path, tire_path, recipe_path, config_path)},
        'importer_sha256': digest(Path(__file__)), 'objects': len(objects), 'links': len(links),
        'tetrahedra': sum(len(a['tetrahedra']) // 4 for a in assets.values()),
        'coordinate_conversion': '(x,y,z) -> (x,z,-y); proper rotation, no reflection',
        'source_records_used': False, 'mass_kg': sum(o['mass'] for o in objects if not o['fixed'])}
    (output / (case + '.provenance.json')).write_text(json.dumps(provenance, indent=2), encoding='utf-8')
    print(target, provenance['objects'], 'objects,', provenance['tetrahedra'], 'tets', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path('D:/Collection/HSC_reimpl'))
    parser.add_argument('--output', type=Path, default=Path('examples/hsc-vehicle'))
    parser.add_argument('--case', choices=['ground_1e8', 'ground_1e6', 'both'], default='both')
    args = parser.parse_args()
    for case in ['ground_1e8', 'ground_1e6'] if args.case == 'both' else [args.case]:
        convert(args.source, args.output, case)
