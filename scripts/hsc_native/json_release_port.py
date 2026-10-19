"""Use nlohmann's hash-verified official release instead of its large repo archive."""
from prepare_build import BUILD
import shutil

def apply():
    source=BUILD/'vcpkg/ports/nlohmann-json'
    target=BUILD/'libuipc/ports/nlohmann-json'
    shutil.copytree(source,target,dirs_exist_ok=True)
    text=(source/'portfile.cmake').read_text()
    end=text.index('\n\nif(NOT DEFINED')
    text='''# Official v3.12.0 release; author SHA256:
# 42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
vcpkg_download_distfile(ARCHIVE
    URLS "https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz"
    FILENAME "nlohmann-json-v3.12.0.tar.xz"
    SHA512 07a47d478bb054367cede3b7864a19a673f486aa3286cc2fd176e5947905354ce61f81e75cad9f2a17d09c1856b973772e24c350bdd7a18b7d77a6307523573c)
vcpkg_extract_source_archive(SOURCE_PATH ARCHIVE "${ARCHIVE}")
'''+text[end:]
    (target/'portfile.cmake').write_text(text)

if __name__=='__main__':apply()
