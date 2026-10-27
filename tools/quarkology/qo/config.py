"""Where things are: the Quark folder, the app's own folder (Quark\\quarkology), the page, config.json."""
import copy
import os

from . import util

DEFAULT_CONFIG = {
    # The Oracle box (SPEC 2, "The Oracle box check"). Only host / user / key are read; q may edit them.
    "oracle": {"host": "129.151.8.15", "user": "ubuntu", "key": "C:\\Users\\user\\Downloads\\SSH Keys\\ssh-key.key"},
}


# Where the app's own code comes from (its self-update and installs.json): a folder of a git clone in Quark\.
# Named only here (and in the hand-over, tools/project_manager/), so the app can move to a repository of its own.
APP_SOURCE_CLONE = "repository"           # Quark\repository\: the game repository's clone, fast-forwarded on testing
APP_SOURCE_DIR = ("tools", "quarkology")  # the app's folder in that clone


class Paths:
    def __init__(self, script_dir, quark=None):
        self.script_dir = os.path.abspath(script_dir)
        self.repo_layout = os.path.basename(self.script_dir) == "quarkology" and \
            os.path.basename(os.path.dirname(self.script_dir)) == "tools"
        if quark:
            self.quark = os.path.abspath(os.path.expanduser(quark))
        elif self.repo_layout:
            # run from a checkout (tools/quarkology/): q's folder layout, docs/LOCAL_BUILD.md
            self.quark = os.path.join(os.path.expanduser("~"), "Desktop", "Quark")
        else:
            self.quark = os.path.dirname(self.script_dir)  # installed: Quark\quarkology\ -> Quark\
        self.pm_dir = os.path.join(self.quark, "quarkology")
        # the folder the app had while it was called Quark Project (qo/migrate.py copies q's files from it)
        self.old_pm_dir = os.path.join(self.quark, "project-manager")
        self.source_dir = os.path.join(self.quark, APP_SOURCE_CLONE, *APP_SOURCE_DIR)
        self.web_dir = os.path.join(self.script_dir, "web")
        self.shell_dir = os.path.join(self.script_dir, "shell")

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
