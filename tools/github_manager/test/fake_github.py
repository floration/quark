"""An in-memory GitHub for the editor's pending changes (stdlib only): the REST reads and writes Pending uses, with the
same interface as server.GitHub (get(path) -> (data, headers, changed), write(method, path, body) -> (status, data),
GhError on failure). The unit tests and the fixture server use it; nothing here talks to the network.

    gh = MemoryGitHub("floration/quark", default="main")
    gh.branches["testing"] = "ab" * 20
    gh.fail_next = (422, "Validation Failed", ["Name must be unique"])   # the next write is refused like GitHub would
"""
import copy
import json
import re

import server as S

PRODUCTION_RULE = {
    "id": 25727099, "name": "production rule", "target": "branch", "source_type": "Repository",
    "source": "floration/quark", "enforcement": "active",
    "conditions": {"ref_name": {"exclude": [], "include": ["~DEFAULT_BRANCH"]}},
    "rules": [{"type": "deletion"}, {"type": "non_fast_forward"},
              {"type": "pull_request", "parameters": {
                  "required_approving_review_count": 0, "dismiss_stale_reviews_on_push": False,
                  "required_reviewers": [], "require_code_owner_review": False, "require_last_push_approval": False,
                  "required_review_thread_resolution": False, "require_extra_approval_for_unattributed_changes": True,
                  "allowed_merge_methods": ["merge"]}},
              {"type": "required_status_checks", "parameters": {
                  "strict_required_status_checks_policy": False, "do_not_enforce_on_create": False,
                  "required_status_checks": [{"context": "from-testing"}]}}],
    "created_at": "2026-10-23T13:59:36.596Z", "updated_at": "2026-10-23T14:01:33.441Z", "bypass_actors": [],
    "current_user_can_bypass": "never",
    "_links": {"html": {"href": "https://github.com/floration/quark/rules/25727099"}}}
TAGGING_RULE = {
    "id": 25727168, "name": "tagging rule", "target": "tag", "source_type": "Repository", "source": "floration/quark",
    "enforcement": "active", "conditions": {"ref_name": {"exclude": [], "include": ["refs/tags/v*"]}},
    "rules": [{"type": "deletion"}, {"type": "non_fast_forward"}, {"type": "creation"}, {"type": "update"}],
    "created_at": "2026-10-23T14:01:05.011Z", "updated_at": "2026-10-23T14:02:07.372Z",
    "bypass_actors": [{"actor_id": 338612073, "actor_type": "User", "bypass_mode": "always"}],
    "current_user_can_bypass": "always",
    "_links": {"html": {"href": "https://github.com/floration/quark/rules/25727168"}}}


def testing_guard(rid=25800001):
    return {"id": rid, "name": "testing guard", "target": "branch", "source_type": "Repository",
            "source": "floration/quark", "enforcement": "active",
            "conditions": {"ref_name": {"exclude": [], "include": ["refs/heads/testing"]}},
            "rules": [{"type": "non_fast_forward"}, {"type": "deletion"}],
            "created_at": "2026-10-23T15:00:00Z", "updated_at": "2026-10-23T15:00:00Z", "bypass_actors": [],
            "current_user_can_bypass": "never",
            "_links": {"html": {"href": "https://github.com/floration/quark/rules/%d" % rid}}}


def sha_of(name):
    """A stable fake sha for a name."""
    import hashlib
    return hashlib.sha1(name.encode("utf-8")).hexdigest()


class MemoryGitHub:
    def __init__(self, repo="floration/quark", default="claude/compassionate-ritchie-07ah6i", rulesets=None):
        self.repo = repo
        self.default = default
        self.branches = {}        # name -> sha
        self.commits = set()      # extra known commits (full shas)
        self.ahead = {}           # sha -> commits ahead of testing (compare); default 0
        self.pulls = []           # {"number", "head": name}
        self.rulesets = dict((r["id"], copy.deepcopy(r)) for r in (rulesets if rulesets is not None
                                                                   else [PRODUCTION_RULE, TAGGING_RULE]))
        self.writes = []          # every write: (method, path, body)
        self.reads = []
        self.fail_next = None     # (status, message, [errors]) for the next write
        self.next_id = 25900000
        self.stamp = 0

    def _now(self):
        self.stamp += 1
        return "2026-10-24T12:%02d:%02dZ" % (self.stamp // 60 % 60, self.stamp % 60)

    def _404(self, path):
        raise S.GhError(404, "gh api %s: Not Found (HTTP 404)" % path.split("?")[0])

    def get(self, path, conditional=True):
        self.reads.append(path)
        base, _, query = path.partition("?")
        q = dict(p.split("=", 1) for p in query.split("&") if "=" in p)
        R = "repos/" + self.repo
        if base == R:
            return {"full_name": self.repo, "default_branch": self.default}, {}, True
        if base.startswith(R + "/git/ref/heads/"):
            name = base[len(R + "/git/ref/heads/"):]
            if name in self.branches:
                return {"ref": "refs/heads/" + name, "object": {"sha": self.branches[name], "type": "commit"}}, {}, True
            self._404(path)
        if base.startswith(R + "/commits/"):
            want = base[len(R + "/commits/"):]
            for sha in list(self.branches.values()) + sorted(self.commits):
                if sha.startswith(want):
                    return {"sha": sha}, {}, True
            raise S.GhError(422, "gh api %s: No commit found for SHA: %s (HTTP 422)" % (base, want))
        if base == R + "/pulls":
            head = q.get("head", "")
            name = head.split(":", 1)[1] if ":" in head else head
            return [{"number": p["number"], "head": {"ref": p["head"]}, "state": "open"} for p in self.pulls
                    if not name or p["head"] == name], {}, True
        m = re.match(r"^%s/compare/testing\.\.\.(.+)$" % re.escape(R), base)
        if m:
            return {"ahead_by": self.ahead.get(m.group(1), 0), "behind_by": 0}, {}, True
        if base == R + "/rulesets":
            return [dict((k, r[k]) for k in ("id", "name", "target", "source_type", "source", "enforcement"))
                    for r in self.rulesets.values()], {}, True
        m = re.match(r"^%s/rulesets/(\d+)$" % re.escape(R), base)
        if m and int(m.group(1)) in self.rulesets:
            return copy.deepcopy(self.rulesets[int(m.group(1))]), {}, True
        self._404(path)

    def write(self, method, path, body=None):
        self.writes.append((method, path, copy.deepcopy(body)))
        if self.fail_next:
            status, msg, errs = self.fail_next
            self.fail_next = None
            raise S.GhError(status, S.gh_write_error(status, json.dumps({"message": msg, "errors": errs}),
                                                     "gh: %s (HTTP %d)" % (msg, status)))
        R = "repos/" + self.repo
        if method == "POST" and path == R + "/git/refs":
            name = body["ref"][len("refs/heads/"):]
            if name in self.branches:
                raise S.GhError(422, S.gh_write_error(422, '{"message": "Reference already exists"}', ""))
            self.branches[name] = body["sha"]
            return 201, {"ref": body["ref"], "object": {"sha": body["sha"]}}
        if method == "DELETE" and path.startswith(R + "/git/refs/heads/"):
            name = path[len(R + "/git/refs/heads/"):]
            if self.branches.pop(name, None) is None:
                raise S.GhError(422, S.gh_write_error(422, '{"message": "Reference does not exist"}', ""))
            return 204, None
        m = re.match(r"^%s/branches/(.+)/rename$" % re.escape(R), path)
        if method == "POST" and m:
            old = m.group(1)
            self.branches[body["new_name"]] = self.branches.pop(old)
            for p in self.pulls:
                if p["head"] == old:
                    p["head"] = body["new_name"]
            return 201, {"name": body["new_name"]}
        if method == "POST" and path == R + "/rulesets":
            rid = self.next_id
            self.next_id += 1
            at = self._now()
            self.rulesets[rid] = dict(copy.deepcopy(body), id=rid, source_type="Repository", source=self.repo,
                                      created_at=at, updated_at=at, current_user_can_bypass="never",
                                      _links={"html": {"href": "https://github.com/%s/rules/%d" % (self.repo, rid)}})
            return 201, copy.deepcopy(self.rulesets[rid])
        m = re.match(r"^%s/rulesets/(\d+)$" % re.escape(R), path)
        if m and int(m.group(1)) in self.rulesets:
            rid = int(m.group(1))
            if method == "PUT":
                self.rulesets[rid].update(copy.deepcopy(body))
                self.rulesets[rid]["updated_at"] = self._now()
                return 200, copy.deepcopy(self.rulesets[rid])
            if method == "DELETE":
                del self.rulesets[rid]
                return 204, None
        raise S.GhError(404, S.gh_write_error(404, '{"message": "Not Found"}', ""))
