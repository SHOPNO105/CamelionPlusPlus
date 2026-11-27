import io
import json
import os
import re
import shutil
import sys
import zipfile
from hashlib import sha256
from urllib.parse import unquote as unquote_url

import requests
from jsonschema import Draft202012Validator, FormatChecker
from requests.adapters import HTTPAdapter
from urllib3.util.retry import Retry

# pywin32 is only needed (and only available) to validate the Windows lists.
try:
    from win32api import GetFileVersionInfo, LOWORD, HIWORD, error as win32api_error
except ImportError:
    GetFileVersionInfo = None

    class win32api_error(Exception):
        pass

# Camelion++ (Linux) lists contain ELF shared objects instead of DLLs.
LINUX_ARCHS = {
    # arch      : (ELF e_machine, pyelftools name)
    'linux-x64': (62, 'EM_X86_64'),
    'linux-arm64': (183, 'EM_AARCH64'),
}
CAMELION_REQUIRED_EXPORTS = ('setInfo', 'getName', 'getFuncsArray', 'beNotified', 'messageProc',
                             'camelionPluginApiVersion')

api_url = os.environ.get('APPVEYOR_API_URL')
has_error = False

# Every run downloads the archive of every plugin in the list, so a single transient hiccup on any
# entry - a dropped connection, a gateway timeout - fails the whole validation and hides the real
# result. Retry those, and give up only when a host is genuinely unreachable.
DOWNLOAD_TIMEOUT_SECONDS = 60
DOWNLOAD_RETRIES = Retry(
    total=3,
    backoff_factor=2,
    status_forcelist=(408, 425, 429, 500, 502, 503, 504),
    allowed_methods=frozenset(['GET']),
    # Let the existing status code check report a persistent failure with its own message.
    raise_on_status=False,
)

# Constants for creating the plugin list overview
C_LINE_BREAK = '\x0d'
C_LINE_FEED = '\x0a'
C_SPACE = ' '
C_SUM_LEN = 100
TMPL_VERT = '&vert;'
TMPL_BR = '<br>'
TMPL_NEW_LINE = '\n'
TMPL_TR_B = '| '
TMPL_TD = ' | '
TMPL_TR_E = ' |'
TMPL_TAB_HEAD = '''| Plugin name | Author | Homepage | Version and link | Description |
|---|---|---|---|---|
'''


def get_version_number(filename):
    info = GetFileVersionInfo(filename, "\\")
    ms = info['FileVersionMS']
    ls = info['FileVersionLS']
    return '.'.join(map(str, [HIWORD(ms), LOWORD(ms), HIWORD(ls), LOWORD(ls)]))


def get_elf_plugin_info(data):
    """Return (machine_name, exported_symbols, camelionPluginVersion) of an ELF shared object."""
    from elftools.elf.elffile import ELFFile
    elf = ELFFile(io.BytesIO(data))
    if elf['e_type'] != 'ET_DYN':
        raise ValueError('not a shared object')
    exports = set()
    version = None
    dynsym = elf.get_section_by_name('.dynsym')
    if dynsym is None:
        raise ValueError('no dynamic symbol table')
    for sym in dynsym.iter_symbols():
        if sym['st_shndx'] == 'SHN_UNDEF' or sym['st_info']['bind'] not in ('STB_GLOBAL', 'STB_WEAK'):
            continue
        exports.add(sym.name)
        if sym.name == 'camelionPluginVersion' and sym['st_info']['type'] == 'STT_OBJECT':
            section = elf.get_section(sym['st_shndx'])
            offset = sym['st_value'] - section['sh_addr']
            version = section.data()[offset:offset + sym['st_size']].split(b'\0')[0].decode('ascii', 'replace')
    return elf['e_machine'], exports, version


def fetch(session, url):
    """Download a plugin archive. file:// URLs are accepted to test lists locally."""
    if url.startswith('file://'):
        class LocalResponse:
            pass
        r = LocalResponse()
        try:
            with open(unquote_url(url[7:]), 'rb') as f:
                r.content, r.status_code = f.read(), 200
        except OSError:
            r.content, r.status_code = b'', 404
        return r
    return session.get(unquote_url(url), timeout=DOWNLOAD_TIMEOUT_SECONDS)


def create_download_session():
    session = requests.Session()
    adapter = HTTPAdapter(max_retries=DOWNLOAD_RETRIES)
    session.mount('https://', adapter)
    session.mount('http://', adapter)
    return session


def post_error(message):
    global has_error

    has_error = True

    message = {
        "message": message,
        "category": "error",
        "details": ""
    }

    if api_url:
        requests.post(api_url + "api/build/messages", json=message)
    else:
        from pprint import pprint
        pprint(message)


def format_version_range(version_range):
    match = re.fullmatch(r'\[(.*?),(.*?)\]', version_range)
    if not match:
        return version_range

    minimum, maximum = match.groups()
    if minimum and maximum:
        return f'{minimum} - {maximum}'
    if minimum:
        return f'>= {minimum}'
    if maximum:
        return f'<= {maximum}'
    return version_range


def format_old_versions_compatibility(compatibility):
    match = re.fullmatch(r'\[(.*?),(.*?)\]\[(.*?),(.*?)\]', compatibility)
    if not match:
        return compatibility

    plugin_minimum, plugin_maximum, npp_minimum, npp_maximum = match.groups()
    plugin_range = format_version_range(f'[{plugin_minimum},{plugin_maximum}]')
    npp_range = format_version_range(f'[{npp_minimum},{npp_maximum}]')
    return f'older plugin versions {plugin_range} for Npp {npp_range}'


def first_two_lines(description):
    if len(description) <= C_SUM_LEN:
        return ""
    i = description.rfind(TMPL_BR, 0, C_SUM_LEN)
    if i != -1:
        return description[:i]
    i = description.rfind(C_SPACE, 0, C_SUM_LEN)
    if i != -1:
        return description[:i]
    return description[:C_SUM_LEN]


def rest_of_text(description):
    return description[len(first_two_lines(description)):]


def gen_pl_table(filename):
    with open(filename, encoding='utf-8') as plugin_list_file:
        pl = json.loads(plugin_list_file.read())
    arch = pl["arch"]
    arch_label = arch if arch.startswith('linux-') else "%s bit" % arch
    tab_text = "## Plugin List - %s%s" % (arch_label, TMPL_NEW_LINE * 2)
    tab_text += "version %s%s" % (pl["version"], TMPL_NEW_LINE * 2)
    tab_text += TMPL_TAB_HEAD

    # Plugin Name = ij.display-name
    # Author = ij.author
    # Homepage = ij.homepage
    # Version and link = "[" + ij.version + " - " + json_file.arch + " bit](" + ij.repository +")"
    # Description = " <details> <summary> " + first_two_lines(ij.description) + " </summary> " rest_of_text(ij.description) +"</details>"
    for plugin in pl["npp-plugins"]:
        tab_line = TMPL_TR_B + plugin["display-name"] + TMPL_TD + plugin["author"] + TMPL_TD + plugin["homepage"] + TMPL_TD
        tab_line += "[%s - %s](%s)" % (plugin["version"], arch_label, plugin["repository"])
        tab_line += TMPL_TD
        descr = plugin["description"]
        descr = descr.replace(C_LINE_FEED, TMPL_BR).replace(C_LINE_BREAK, '').replace("|", TMPL_VERT)
        summary = first_two_lines(descr)
        rest = rest_of_text(descr)
        if summary:
            tab_line += " <details> <summary> %s </summary> %s </details>" % (summary, rest)
        else:
            tab_line += rest
        tab_line += TMPL_TR_E + TMPL_NEW_LINE
        tab_text += tab_line
    return tab_text


def unique_json_keys_check(plugin, displaynames, foldernames, repositories):

    found = False
    for name in displaynames :
       if plugin["display-name"] == name :
           post_error(f'{plugin["display-name"]}: non unique display-name entry')
           found = True
    if found == False:
           displaynames.append(plugin["display-name"])

    found = False
    for folder in foldernames :
       if plugin["folder-name"] == folder :
           post_error(f'{plugin["folder-name"]}: non unique folder-name entry')
           found = True
    if found == False:
       foldernames.append(plugin["folder-name"])

    found = False
    for repo in repositories :
       if plugin["repository"] == repo :
           post_error(f'{plugin["repository"]}: non unique repository entry')
           found = True
    if found == False:
       repositories.append(plugin["repository"])


def parse(filename):
    try:
        with open("pl.schema", encoding='utf-8') as schema_file:
            schema = json.loads(schema_file.read())
        schema = Draft202012Validator(schema, format_checker=FormatChecker())
    except ValueError as e:
        post_error("pl.schema - " + str(e))
        return

    try:
        with open(filename, encoding='utf-8') as plugin_list_file:
            pl = json.loads(plugin_list_file.read())
    except ValueError as e:
        post_error(filename + " - " + str(e))
        return

    for error in schema.iter_errors(pl):
        post_error(error.message)

    foldernames = []
    displaynames = []
    repositories = []

    if os.path.exists("./" + provided_architecture):
        shutil.rmtree("./" + provided_architecture, True)

    os.mkdir("./" + provided_architecture)
    download_session = create_download_session()
    for plugin in pl["npp-plugins"]:
        print(plugin["display-name"], end='')

        compatibility_messages = []
        if 'camelion-compatible-versions' in plugin:
            compatibility_messages.append(
                f'REQUIRES Camelion++ {format_version_range(plugin["camelion-compatible-versions"])}'
            )
        if 'npp-compatible-versions' in plugin:
            compatibility_messages.append(
                f'REQUIRES Npp {format_version_range(plugin["npp-compatible-versions"])}'
            )
        if 'old-versions-compatibility' in plugin:
            compatibility_messages.append(
                format_old_versions_compatibility(plugin["old-versions-compatibility"])
            )

        if not compatibility_messages:
            print()
        else:
            print(f' *** {"; ".join(compatibility_messages)} ***')

        try:
            response = fetch(download_session, plugin["repository"])
        except requests.exceptions.RequestException as e:
            post_error(f'{plugin["display-name"]}: {str(e)}')
            continue

        if response.status_code != 200:
            post_error(f'{plugin["display-name"]}: failed to download plugin. Returned code {response.status_code}.')
            continue

        # Calculate the hash and compare it with the expected value
        hash = sha256(response.content).hexdigest()
        if plugin["id"].lower() != hash.lower():
            post_error(f'{plugin["display-name"]}: Invalid hash. Got {hash.lower()} but expected {plugin["id"]}.')
            continue

        # Ensure it is a valid zip file
        try:
            zip = zipfile.ZipFile(io.BytesIO(response.content))
        except zipfile.BadZipFile as e:
            post_error(f'{plugin["display-name"]}: Invalid zip file.')
            continue

        if provided_architecture in LINUX_ARCHS:
            if validate_linux_plugin(plugin, zip):
                unique_json_keys_check(plugin, displaynames, foldernames, repositories)
            continue

        # The expected DLL name
        dll_name = f'{plugin["folder-name"]}.dll'.lower()

        # While Notepad++ is not case-sensitive, extracting files from the zip file is, so we convert
        # both the expected file name and the extracted file names to lowercase for accurate matching
        for file in zip.namelist():
            if dll_name == file.lower():
                dll_name = file
                break
        else:
            post_error(f'{plugin["display-name"]}: Zip file does not contain {plugin["folder-name"]}.dll.')
            continue

        with zip.open(dll_name) as dll_file, open("./" + provided_architecture + "/" + dll_name, 'wb') as f:
            f.write(dll_file.read())

        version = plugin["version"]

        # Fill in any missing numbers as zeros to ensure a complete version number
        version = version + (3 - version.count('.')) * ".0"

        if GetFileVersionInfo is None:
            post_error(f'{plugin["display-name"]}: pywin32 is required to check DLL versions (run the Windows lists on Windows).')
            continue
        try:
            dll_version = get_version_number("./" + provided_architecture + "/" + dll_name)
        except win32api_error:
            post_error(f'{plugin["display-name"]}: Does not contain any version information.')
            continue

        if dll_version != version:
            post_error(f'{plugin["display-name"]}: Unexpected DLL version. The DLL version '
                       f'detected is {dll_version}, but the expected version is {version}.')
            continue


        #check uniqueness of json folder-name, display-name and repository
        unique_json_keys_check(plugin, displaynames, foldernames, repositories)



def validate_linux_plugin(plugin, zip):
    """Camelion++ plugins: <folder-name>.so at the zip root, right CPU, required exports, matching version."""
    so_name = f'{plugin["folder-name"]}.so'
    names = {n.lower(): n for n in zip.namelist() if '/' not in n.rstrip('/')}
    if so_name.lower() not in names:
        post_error(f'{plugin["display-name"]}: Zip file does not contain {so_name} at its root.')
        return False
    data = zip.read(names[so_name.lower()])
    with open("./" + provided_architecture + "/" + so_name, 'wb') as f:
        f.write(data)
    try:
        machine, exports, so_version = get_elf_plugin_info(data)
    except Exception as e:
        post_error(f'{plugin["display-name"]}: {so_name} is not a valid ELF shared object ({e}).')
        return False
    expected_machine = LINUX_ARCHS[provided_architecture][1]
    if machine != expected_machine:
        post_error(f'{plugin["display-name"]}: {so_name} is built for {machine}, expected {expected_machine}.')
        return False
    missing = [e for e in CAMELION_REQUIRED_EXPORTS if e not in exports]
    if missing:
        post_error(f'{plugin["display-name"]}: {so_name} does not export {", ".join(missing)} '
                   f'(see CamelionPluginInterface.h).')
        return False
    if so_version is None:
        post_error(f'{plugin["display-name"]}: {so_name} has no camelionPluginVersion (use CAMELION_PLUGIN_VERSION).')
        return False
    expected = plugin["version"] + (3 - plugin["version"].count('.')) * ".0"
    found = so_version + (3 - so_version.count('.')) * ".0"
    if expected != found:
        post_error(f'{plugin["display-name"]}: Unexpected plugin version. {so_name} reports {so_version}, '
                   f'but the list says {plugin["version"]}.')
        return False
    return True


ARCHITECTURE_FILENAMES_MAPPING = {
    'x86': ("src/pl.x86.json", "doc/plugin_list_x86.md"),
    'x64': ("src/pl.x64.json", "doc/plugin_list_x64.md"),
    'arm64': ("src/pl.arm64.json", "doc/plugin_list_arm64.md"),
    'linux-x64': ("src/pl.linux-x64.json", "doc/plugin_list_linux-x64.md"),
    'linux-arm64': ("src/pl.linux-arm64.json", "doc/plugin_list_linux-arm64.md"),
}
ARCHITECTURE_OPTIONS = ", ".join(ARCHITECTURE_FILENAMES_MAPPING.keys())
ARCHITECTURE_OPTIONS = f"{ARCHITECTURE_OPTIONS.rpartition(',')[0]}, or{ARCHITECTURE_OPTIONS.rpartition(',')[-1]}"
if len(sys.argv) > 1:
    provided_architecture = sys.argv[1].lower()
else:
    provided_architecture = input(f'Please provide the target architecture ({ARCHITECTURE_OPTIONS}): ').lower()
if provided_architecture in ARCHITECTURE_FILENAMES_MAPPING:
    json_file, output_file = ARCHITECTURE_FILENAMES_MAPPING[provided_architecture]
elif provided_architecture == "all_md":
    for key in ARCHITECTURE_FILENAMES_MAPPING.keys():
        json_file, output_file = ARCHITECTURE_FILENAMES_MAPPING[key]
        with open(output_file, "w", encoding='utf-8') as md_file:
            md_file.write(gen_pl_table(json_file))
    if has_error:
        sys.exit(-2)
    else:
        sys.exit()
else:
    json_file, output_file = ARCHITECTURE_FILENAMES_MAPPING['x86']
print(f'Provided architecture: {provided_architecture}.')
parse(json_file)
with open(output_file, "w", encoding='utf-8') as md_file:
    md_file.write(gen_pl_table(json_file))

if has_error:
    sys.exit(-2)
else:
    sys.exit()
