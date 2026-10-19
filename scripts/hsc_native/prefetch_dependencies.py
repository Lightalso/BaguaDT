"""Cache a renamed official repository using the unchanged vcpkg content hash."""
from prepare_build import BUILD
import hashlib, urllib.request

def prefetch():
    dest=BUILD/'vcpkg_downloads/oneapi-src-oneTBB-v2022.1.0.tar.gz'
    expected='7582748f7d0e0ab46ea6ee7771dfaf7fc08ca7ab7f274fb3373eae0e3411aaafbac192ece15008d9a3d9e8566f8737f96f3f4b5ccf11449ac089d5cd9ebb9eab'
    if not dest.exists():
        url='https://codeload.github.com/uxlfoundation/oneTBB/tar.gz/refs/tags/v2022.1.0'
        with urllib.request.urlopen(url,timeout=60) as r:data=r.read()
        if hashlib.sha512(data).hexdigest()!=expected:raise RuntimeError('TBB source hash mismatch')
        dest.write_bytes(data)
    if hashlib.sha512(dest.read_bytes()).hexdigest()!=expected:raise RuntimeError('TBB cache hash mismatch')

if __name__=='__main__':prefetch()
