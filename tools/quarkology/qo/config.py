"""Where things are: the Quark folder, the app's own folder (Quark\\quarkology), the page, config.json."""
import copy
import os

from . import util

DEFAULT_CONFIG = {
    # The Oracle box (SPEC 2, "The Oracle box check"). Only host / user / key are read; q may edit them.
    "oracle": {"host": "129.151.8.15", "user": "ubuntu", "key": "C:\\Users\\user\\Downloads\\SSH Keys\\ssh-key.key"},
}


# Where the app's own code comes from (its self-update and installs.json): a folder of a git clone in Quark\.
# Since 1.20 that is the Quarkology repository (floration/quarkology, private): Quark\quarkology-repo\, on branch
# main, cloned and fast-forwarded by qo/updater.py. Until that clone exists (or when it can't be made) the app keeps
# running from the game repository's one-release hand-over copy, Quark\repository\tools\quarkology\ (LEGACY_*).
APP_SOURCE_CLONE = "quarkology-repo"      # Quark\quarkology-repo\: the Quarkology repository's clone, branch main
APP_SOURCE_DIR = ("quarkology",)          # the app's folder in that clone
APP_BRANCH = "main"
APP_REPO_URL = "https://github.com/floration/quarkology.git"  # (the game clone's own origin form wins: updater.py)
GAME_CLONE = "repository"                 # Quark\repository\: the game repository's clone, fast-forwarded on testing
GAME_BRANCH = "testing"
LEGACY_SOURCE_DIR = ("tools", "quarkology")  # the hand-over copy in the game clone


def _is_game_checkout(d):
    return os.path.isfile(os.path.join(d, "CMakeLists.txt")) and os.path.isdir(os.path.join(d, "src", "game"))


class Paths:
    def __init__(self, script_dir, quark=None, app_origin=None):
        self.script_dir = os.path.abspath(script_dir)
        parent = os.path.dirname(self.script_dir)
        # run from a checkout: the game's tools/quarkology/ (the hand-over copy), or the Quarkology repository's
        # quarkology/ (a clone with wip/ next to it)
        self.repo_layout = os.path.basename(self.script_dir) == "quarkology" and (
            (os.path.basename(parent) == "tools" and _is_game_checkout(os.path.dirname(parent))) or
            os.path.isdir(os.path.join(parent, "wip")))
        if quark:
            self.quark = os.path.abspath(os.path.expanduser(quark))
        elif self.repo_layout and os.path.basename(parent).lower() == APP_SOURCE_CLONE:
            self.quark = os.path.dirname(parent)  # Quark\quarkology-repo\quarkology\ -> Quark\
        elif self.repo_layout:
            # another checkout: q's folder layout, docs/LOCAL_BUILD.md
            self.quark = os.path.join(os.path.expanduser("~"), "Desktop", "Quark")
        else:
            self.quark = parent  # installed: Quark\quarkology\ -> Quark\
        self.pm_dir = os.path.join(self.quark, "quarkology")
        # the folder the app had while it was called Quark Project (qo/migrate.py copies q's files from it)
        self.old_pm_dir = os.path.join(self.quark, "project-manager")
        self.app_repo = os.path.join(self.quark, APP_SOURCE_CLONE)
        self.game_repo = os.path.join(self.quark, GAME_CLONE)
        self.app_source_dir = os.path.join(self.app_repo, *APP_SOURCE_DIR)
        self.legacy_source_dir = os.path.join(self.game_repo, *LEGACY_SOURCE_DIR)
        self.app_origin = app_origin  # (None: worked out from the game clone's origin, updater.app_origin_url)
        self.web_dir = os.path.join(self.script_dir, "web")
        self.shell_dir = os.path.join(self.script_dir, "shell")

    @property
    def source_dir(self):
        """Where the app's code comes from now: the Quarkology repository's clone once it has the app, else the game
        repository's hand-over copy (tools/quarkology, one release)."""
        if os.path.isfile(os.path.join(self.app_source_dir, "server.py")):
            return self.app_source_dir
        return self.legacy_source_dir

    def has_app_repo(self):
        return os.path.isdir(os.path.join(self.app_repo, ".git")) and \
            os.path.isfile(os.path.join(self.app_source_dir, "server.py"))

    def q(self, *parts):
        return os.path.join(self.quark, *parts)

    def pm(self, *parts):
        return os.path.join(self.pm_dir, *parts)

    def ensure_pm_dir(self):
        os.makedirs(self.pm_dir, exist_ok=True)


def load_config(paths):
    """config.json merged over the defaults (a missing or broken file gives the defaults)."""
    cfg = copy.deepcopy(DEFAULT_CONFIG)
    path = paths.pm("config.json")
    try:
        user = util.read_json_file(path)
    except FileNotFoundError:
        return cfg
    except (OSError, ValueError) as e:
        util.log("config.json unreadable, using defaults (%s)" % type(e).__name__)
        return cfg
    if isinstance(user, dict):
        for k, v in user.items():
            if isinstance(v, dict) and isinstance(cfg.get(k), dict):
                cfg[k].update(v)
            else:
                cfg[k] = v
    return cfg


def write_default_config(paths):
    """config.json only when missing (setup.ps1 calls server.py --init). True when written."""
    path = paths.pm("config.json")
    if os.path.exists(path):
        return False
    paths.ensure_pm_dir()
    util.write_json_atomic(path, DEFAULT_CONFIG)
    return True
