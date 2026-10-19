"""Use already-pinned ports locally instead of fetching the entire registry history."""
from prepare_build import BUILD, ROOT
import base64, json, urllib.request

def get(url):
    req=urllib.request.Request(url,headers={'User-Agent':'HC-reproduction','Accept':'application/vnd.github+json'})
    with urllib.request.urlopen(req,timeout=90) as r:return json.load(r)

def prepare():
    ports=BUILD/'libuipc/ports'
    records=[]
    for name,version in [('fmt','10.2.1'),('spdlog','1.12.0')]:
        versions=json.loads((BUILD/f'vcpkg/versions/{name[0]}-/{name}.json').read_text())['versions']
        entry=next(x for x in versions if any(x.get(k)==version for k in ('version','version-semver','version-string')) and x.get('port-version',0)==0)
        dest=ports/name;dest.mkdir(exist_ok=True)
        if not ((ROOT/'local_ports.json').exists() and (dest/'portfile.cmake').exists()):
            tree=get('https://api.github.com/repos/microsoft/vcpkg/git/trees/'+entry['git-tree'])
            for f in tree['tree']:
                if f['type']!='blob':raise ValueError('Unexpected nested port')
                target=dest/f['path']
                if not target.exists():target.write_bytes(base64.b64decode(get(f['url'])['content']))
        records.append({'port':name,'version':version,'git_tree':entry['git-tree']})
    commit='7a5a8167ff2159564076a3e0f4b025695e9df91e'
    dest=ports/'octree';dest.mkdir(exist_ok=True)
    if not ((ROOT/'local_ports.json').exists() and (dest/'portfile.cmake').exists()):
        tree=get(f'https://api.github.com/repos/spiriMirror/vcpkg/contents/ports/octree?ref={commit}')
        for f in tree:
            target=dest/f['name']
            if not target.exists():
                with urllib.request.urlopen(f['download_url'],timeout=60) as r:target.write_bytes(r.read())
    records.append({'port':'octree','registry_commit':commit})
    cfg=BUILD/'build/vcpkg-configuration.json'
    data=json.loads(cfg.read_text())
    data['default-registry']={'kind':'builtin','baseline':'dd3097e305afa53f7b4312371f62058d2e665320'}
    data['registries']=[]
    data['overlay-ports']=['../libuipc/ports','../vcpkg/ports']
    cfg.write_text(json.dumps(data,indent=2))
    manifest=BUILD/'build/vcpkg.json'
    manifest_data=json.loads(manifest.read_text())
    for dep in manifest_data['dependencies']:
        if dep['name']=='tbb':dep['default-features']=False
    manifest.write_text(json.dumps(manifest_data,indent=2))
    # Keep CMake's manifest regeneration on the same local, pinned resolution.
    script=BUILD/'libuipc/scripts/gen_vcpkg_json.py'
    text=script.read_text()
    marker='    config_change = write_vcpkg_configuration(args)'
    if 'HSC_LOCAL_REGISTRY' not in text:
        text=text.replace(marker,'''    # HSC_LOCAL_REGISTRY: identical pinned ports, cached locally.
    base_vcpkg_configuration['default-registry'] = {'kind': 'builtin', 'baseline': VCPKG_BASE_LINE}
    base_vcpkg_configuration['registries'] = []
'''+marker)
        script.write_text(text)
    if 'HSC_TBB_NO_HWLOC' not in text:
        marker='    config_change = write_vcpkg_configuration(args)'
        text=text.replace(marker,"    # HSC_TBB_NO_HWLOC: keep TBB, disable optional CPU topology binding.\n    for dep in base_vcpkg_json['dependencies']:\n        if dep['name'] == 'tbb': dep['default-features'] = False\n"+marker)
        script.write_text(text)
    # Every other dependency comes from the checked-out baseline, avoiding
    # historical minimum-version tree lookups in a shallow clone.
    marker="    config_change = write_vcpkg_configuration(args)"
    if 'HSC_BASELINE_PORTS' not in text:
        text=text.replace(marker,"    # HSC_BASELINE_PORTS\n    _old_overlay = overlay_ports_entry\n    def overlay_ports_entry(output_dir): return _old_overlay(output_dir)\n"+marker)
        # write_vcpkg_configuration computes overlay-ports itself.
        text=text.replace("[overlay_ports_entry(args.output_dir)]", "[overlay_ports_entry(args.output_dir), '../vcpkg/ports']")
        script.write_text(text)
    (ROOT/'local_ports.json').write_text(json.dumps(records,indent=2))
    print('Pinned ports cached; using local vcpkg baseline',flush=True)

if __name__=='__main__':prepare()
